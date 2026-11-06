# Changelog
## 1.11.0 — SMART OPS, ⚙ OPÇÕES E PRESETS
- **Smart Ops**: 24 perfis no Deresonate (novos: Sopros, Podcast, Sala, Voz ao vivo, Sample/loop) e 20 em De-Ess, Match e Unmask; os perfis aparecem como botões numa grelha com scroll (1 clique; clicar de novo volta a "personalizado").
- **OUVIR segue a seleção**: com OUVIR ligado, clicar noutro ponto/banda passa a ouvir essa banda.
- **Roda dentada ⚙** no cabeçalho com Idioma (Português/English), Cor (PRIME/OURO/ÍNDIGO) e Terminar sessão; os botões PT|EN e cor saíram do cabeçalho.
- **Presets**: botão de preset largo (já não corta "Init (Flat)"), setas < > para trocar de predefinição, Undo/Redo e A/B ao lado.
- **Nomes mais claros**: COLETIVO → **OUTRAS PISTAS** (curvas das outras instâncias do projeto); ZONAS → **CONFLITOS** (masking em vermelho, espaço em violeta); tooltips explicam o que fazem.
- Não testado num DAW real.
## 1.10.0 — NOVA INTERFACE
- **Nome**: o produto passa a chamar-se **PrimEQ Pro by Piradex** (cabeçalho, bundle VST3/AU, instaladores e documentação). O código de plugin, o BUNDLE_ID, as sessões guardadas e a licença não mudam: as sessões antigas abrem normalmente.
- **Interface reorganizada**: barra lateral esquerda com as ferramentas (Smart Ops, Studio, Assistente, Mixer, Mapa 3D, Coletivo, Zonas, ABX); em baixo, uma só linha com a banda seleccionada (Freq/Gain/Q grandes, tipo, DYN, Listen/Copiar/Colar/Apagar) e o cartão SAÍDA (output, Auto Gain, Delta, Sidechain).
- **EXPERT** (botão no topo) abre uma gaveta com 4 cartões: DINÂMICA, DE-HARSH, CARÁCTER · T/S e ANÁLISE · MEDIÇÃO (FFT, Vista, Tilt, Escala, Peak Hold, Freeze, latência/CPU). O estado fica guardado na sessão. Substitui o menu Simple/Advanced/Expert (o antigo modo Simple desapareceu; as sessões antigas abrem em normal ou EXPERT). EXPERT liga também Cenários e o relatório do Assistente.
- **Medidor de saída L/R** à direita do espectro, com escala e CLIP; só um medidor lê o nível de cada vez.
- **Idioma**: botões **PT | EN** sempre visíveis no cabeçalho (o activo fica destacado); a escolha é guardada.
- Limites assumidos: janela mínima 1100×740 (os knobs ficam apertados com a gaveta aberta); não testado num anfitrião real (DAW) nem com os instaladores noutras máquinas; o ficheiro do plugin passa a ter o nome com capitalização nova (o macOS e o Windows tratam-no como o mesmo caminho).
## 1.9.0 — VISUAL MIXER
- **VISUAL MIXER** (botão MIXER): superfície 2D com um nó por pista (mesmo processo da DAW). **X = pan, Y = ganho da pista, extensão do nó = largura estéreo.** Arrastar o nó muda a pista; arrastar as pontas muda a largura; duplo clique repõe. Lista de pistas com valores, **Snapshots A/B/C** (Set guarda, a letra repõe) e **Balance Assistant** (ponto de partida de nível/pan/largura a partir do nível medido e das prioridades, com DESFAZER). Anéis vermelhos = pistas em choque (as mesmas do Mapa 3D). O Mapa 3D continua a diagnosticar; o Visual Mixer age.
- Novos parâmetros por instância, guardados no estado e automatizáveis: **Track Pan** (-100..+100 %, balanço com lei cos) e **Track Width** (0..200 %, M/S). O ganho Y é o "Output Gain" já existente. Neutro (0 % / 100 %) = saída idêntica à anterior (testado).
- Balance Assistant: voz = 0 dB (referência); alvos relativos à voz: kick -4, baixo -5, prioridade 4 -6, 2 -9, 1 -12 dB; kick, baixo e voz ao centro (kick/baixo com largura 60 %); restantes alternam L/R a 25, 35 e 45 %. Ponto de partida para validar de ouvido, não uma mistura automática.
- As sugestões de pan do Assistente já não dizem que o plugin não tem panner.
- Limites assumidos: só vê instâncias no mesmo processo da DAW; snapshots e DESFAZER vivem enquanto o projecto está aberto (não são guardados no estado do plugin); o nível medido para o Balance Assistant exige ANALISAR ligado e material representativo; pan/width ficam depois do EQ, por isso a posição na cadeia da DAW importa; não substitui os faders da DAW (somam-se).
## 1.8.0 — SPECTRAL
- **T/S espectral (HPSS)**: novo "T/S MOTOR" = *Envelope* (como na 1.7, sem latência) ou *Spectral* (STFT 2048, hop 512, separação por medianas tempo/frequência, máscara suave). Separa transiente e tonal em frequência, por isso é mais robusto com vibrato forte e material denso que o detetor de envelope. Só em Zero Latency; **declara +3584 amostras de latência** (compensada pela DAW). Transiente + Tonal = entrada atrasada exatamente (nulo medido a -300 dB; banda neutra = entrada atrasada).
- **Confiança medida no Smart Ops**: já não é só o tempo de material. Mede a repetibilidade do achado (material dividido em metades alternadas de 1 s, correlação entre metades), a estabilidade e a proeminência. O modo Auto só aplica com confiança média/alta *medida*. Determinístico, sem IA. Mede se o achado é repetível e claro, **não** se o corte soa melhor.
- **Mais presets no Smart Ops**: 42 perfis (De-resonate 19, De-ess 9, Match 6, Unmask 8), cada um com valores iniciais e gama de frequências própria (voz masculina/feminina, rap, kick, bass, guitarra, overheads, piano, mix bus, sibilância...).
- **Mapa 3D da mistura** (botão MAPA 3D): todas as pistas analisadas em perspetiva (frequência · nível relativo · pista ou pan), rodar com o rato, zoom, isolar uma pista, planos vermelhos onde duas ou mais pistas fortes competem.
- **Overlay coletivo** (botão COLETIVO): curvas das outras pistas (nível relativo) e bandas em choque desenhadas no gráfico principal.
- Limites assumidos: Mapa 3D e coletivo só veem instâncias do PRIMEQ PRO no mesmo processo da DAW e com ANALISAR ligado (DAWs que isolam plugins em processos separados não mostram as outras pistas); o "choque" é uma heurística de níveis próximos, não uma medida de mascaramento perceptual; o T/S espectral não foi ouvido em DAW real nem validado com auval.
## 1.7.0 — PRIME
- **Skins**: botão de cor no topo alterna entre **PRIME** (grafite + teal), **OURO** (grafite quente + dourado) e **ÍNDIGO** (azul/laranja da paleta Okabe-Ito, seguro para daltonismo). Aplica-se logo, fica guardado e segue todas as instâncias abertas.
- Polimento visual: cabeçalho com gradiente e linha de acento, botões e potenciómetros com relevo e brilho, curva total com halo, dicas na cor da skin.
- **EQ Transiente / Tonal por banda**: cada banda pode atuar em *Both*, só nos *Transient* (ataques) ou só no *Tonal* (parte sustentada). Máscara complementar (mT + mS = 1), sem latência extra, com *T/S Separation* e *T/S Smooth* globais. Funciona em Zero Latency (em Natural/Linear Phase as bandas T/S atuam como Both). Com lookahead ligado, a máscara vê o sinal sem atraso. Medido: um seno estável passa intacto numa banda Transient (0,0 dB) e é cortado 12 dB numa banda Tonal.
- **Smart Ops com modos**: *Analisar* (só evidências, nada é aplicado), *Assistir* (pré-visualiza, você aplica), *Auto (opt-in)* (aplica ao parar o Learn só com confiança média/alta; Revert desfaz). Cada proposta mostra confiança (material recolhido vs necessário) e risco. Determinístico: não há IA nem rede.
- Corrigido: dica do botão STUDIO aparecia com acentos estragados.
- Limites assumidos: a máscara T/S é um detetor de envelope (não uma separação espectral STFT); Vozes com vibrato forte ou materiais muito densos podem dar falsos "transientes". Vistas 3D e multi-instância coletiva da especificação ficam para versões futuras.
## 1.6.0 — STUDIO
- Novo painel **STUDIO** (botão no topo): **ALVO** (curvas-alvo por género: Kizomba, Semba, Kuduro, Amapiano, Afrobeat, Hip-Hop, Trap, Pop, Rock, Eletrónica, Voz; ajustáveis; "gravar a minha"; desvio desenhado no gráfico; propor/aplicar bandas; sugerir o preset de fábrica mais próximo), **TRADUTOR** (telemóvel, colunas pequenas, auriculares, carro, mono: o que desaparece, e "ouvir assim"), **PALAVRAS** (ex.: "menos lama, mais presença", PT/EN, com pré-visualização), **COFRE** (decisões com motivo, restauro por etapas, ficha bilingue em HTML, enviar a assinatura para outras pistas) e **ENSINO** (explica ao vivo o que cada banda faz).
- **ABX por banda**: testa às cegas só uma banda (A = sem a banda, B = com a banda) com o nível igualado.
- Limites assumidos: os alvos são curvas heurísticas (não medidas em faixas comerciais); o Tradutor usa respostas genéricas por categoria de equipamento; as palavras são um dicionário de termos, não compreensão de linguagem; só há medição de 80 Hz a 12 kHz.
## 1.5.0
- Novo: idioma Português / English. Botão PT/EN no topo; troca imediata e guardada. Sem escolha, segue o idioma do sistema.
- Traduzidos: menus, botões, dicas, guia inicial, teste ABX, Assistente (rótulos). As frases de diagnóstico geradas pelo Assistente e o relatório exportado continuam em português nesta versão.
## 1.4.1
- CORRECÇÃO: o PRIMEQ PRO tinha o mesmo código de plugin (PxEQ) que o EQMotion, por isso a DAW (Studio One) escondia um deles. Novo código: PxPQ (VST3 CID e AU únicos). Sessões antigas que usavam o PRIMEQ PRO precisam de o inserir de novo.
## 1.4.0
- Filtro Bell matched (Vicanek): cramping perto de Nyquist 7,4 → 1,2 dB (44,1/48 kHz).
- Segurança: hashes de senhas fora do repositório (injectados no CI); validação online via PCC com resposta assinada Ed25519 (função `validate` em pcc/supabase).
- Novo: overlay ZONAS no gráfico, teste ABX cego, guia de arranque, manual (Docs/MANUAL.md).
- CI: pluginval (strictness 5) em macOS e Windows.
## 1.3.0
- Nome do produto: **PRIMEQ PRO by Piradex** (instaladores PRIMEQPRO-*).
- Presets de fábrica: 114 originais em 14 categorias (Vocals, Drums, Bass, Guitar, Keys, Synth, Orquestra, Mixbus, Master, Corretivo, Géneros [Kizomba, Semba, Kuduro, Amapiano, Afrobeat, Hip-Hop, Trap], Sends, Utility). Menu por submenus.
- Presets criados de raiz; não copiam conteúdo de terceiros.
- Medição de cramping do biquad RBJ a 44,1/48/96 kHz (ver Docs/TESTING.md); correcção em curso.
- Testes: 101 (DSP) + 81 (integração).
## 1.2.0
- Novo: Assistente (dossiê EQNOVA X): Masking, Espaço (mapa frequência×panorama, mono safety, M/S), Mix Doctor, Intent-Based EQ, Mix Consequence Engine, relatório exportável.
- Novo: modos Simple / Advanced / Expert; grupos G1–G4; indicador de Δ loudness; propostas com tipo/Mid-Side.
- Testes: 101 (DSP) + 76 (integração).
## 1.1.0
- Novo: De-harsh, Character (3 modos, oversampling), Lookahead, De-Ess, Unmask (sidechain), Reference Match, Morph A↔B.
- Novo: aba MIX UNMASK (pistas da sessão, comparação EQ/Dinâmica/Sidechain, aplicar, guia de routing).
- Novos presets de fábrica; GUI redimensionável (1000×740 a 2600×1700).
- Testes: 80 (DSP) + 65 (integração).
## 1.0.0
- Primeira versão: 24 bandas, 8 tipos, declives até 96 dB/oct + Brickwall, fases ZL/Natural/Linear, EQ dinâmico com sidechain, analisador, Deresonate, A/B, presets, undo, licenciamento PBKDF2.
- Não incluído: De-harsh, Character, Reference Match, Unmask, Morph, De-ess, lookahead (marcos posteriores do brief).
