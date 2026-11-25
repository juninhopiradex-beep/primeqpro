# PrimEQ Pro by Piradex

EQ profissional paramétrico + dinâmico (Beatfreak Music · Plugins by Piradex). C++20 / JUCE 8.

> **Versão atual: 1.0.2** (todos os plugins Piradex passaram a ter a mesma numeração; as versões abaixo de "Histórico" são da numeração antiga).
>
> * **Login:** obrigatório desde a 1.0.1 — senha master, senha beta (válida até 31/12/2026) ou chave de licença validada online no PCC. Sem sessão, o áudio passa intacto.
> * **Instaladores (loja www.beatfreakstudio.com/loja):** Mac Apple Silicon (`…-macOS-AppleSilicon.pkg`), Mac Intel com macOS 10.13 ou mais recente (`…-macOS-Intel-macOS10.13.pkg`), Windows (`…-Windows-Setup.exe`) e AAX para Pro Tools (`…-AAX-*.zip`, ver LEIA-ME-AAX.txt).
> * **Idioma e boas-vindas (1.0.2):** interface em Português, English e 中文 (mandarim), escolhida na roda dentada e comum a todos os plugins Piradex; ecrã de login Piradex igual em todos; pop-up de tutorial e novidades nas 3 primeiras aberturas de cada versão (reabre pelo menu).
> * **Criador e desenvolvedor:** Ricardo Amorim / Juninho Piradex.

![Editor](Docs/screenshots/editor_screenshot.png)
![Assistente - Masking](Docs/screenshots/assist_mask.png)
![Assistente - Espaço](Docs/screenshots/assist_space.png)
![Assistente - Intenção](Docs/screenshots/assist_intent.png)

## Características (resumo — ver [Docs/FEATURES.md](Docs/FEATURES.md) para o estado real)
- 24 bandas, 8 tipos de filtro, declives 6–96 dB/oct + Brickwall finito.
- Modos de fase: Zero Latency (IIR), Natural, Linear (FIR com PDC reportada ao anfitrião).
- EQ dinâmico por banda com sidechain externo; analisador FFT real; Smart Ops "Deresonate" (determinístico).
- Licenciamento offline PBKDF2 (master/beta) igual aos outros plugins Piradex.

## Formatos e instaladores
| Plataforma | Saída | Estado |
|---|---|---|
| macOS (universal Intel+Apple Silicon, ≥10.13) | VST3, AU, Standalone, `.pkg` | compilado só no CI; **não testado em DAW real** |
| Windows x64 | VST3, Standalone, `Setup.exe` (Inno Setup) | compilado só no CI; **não testado em DAW real** |
| AAX | — | **não incluído**: exige SDK Avid + assinatura PACE (ver [Docs/AAX.md](Docs/AAX.md)) |

Binários em *Actions → último run → Artifacts* ou em *Releases*. Instalação e resolução de problemas (Catalina): [Docs/INSTALL.md](Docs/INSTALL.md).

## Compilar
```
cmake -B build -DPQ_BUILD_TESTS=ON -DPQ_BUILD_SMOKE=ON
cmake --build build --config Release
```
Documentação: [Arquitectura](Docs/ARCHITECTURE.md) · [Testes](Docs/TESTING.md) · [Licenças](Docs/LICENSES.md) · [Licenciamento/PCC](Docs/LICENSING_PCC.md) · [Assinatura](Packaging/SIGNING.md) · [Changelog](CHANGELOG.md)
