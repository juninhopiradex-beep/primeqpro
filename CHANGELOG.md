# Changelog
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
