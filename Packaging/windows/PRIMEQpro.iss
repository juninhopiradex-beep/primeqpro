; PRIMEQ pro by Piradex — instalador Windows (Inno Setup). Instala o VST3 em C:\Program Files\Common Files\VST3

#define MyName "PRIMEQ pro by Piradex"
#define MyVersion "1.2.0"
#define MyPublisher "Ricardo Amorim / Juninho Piradex"

; Caminhos passados pelo workflow (/D). Valores por omissao para build local.
#ifndef VST3SRC
  #define VST3SRC "out\PRIMEQ pro by Piradex.vst3"
#endif
#ifndef OUTDIR
  #define OUTDIR "."
#endif

[Setup]
AppId={{B7E1D4C2-5A31-4F6E-9C8B-2D7A60E19F43}
AppName={#MyName}
AppVersion={#MyVersion}
AppPublisher={#MyPublisher}
AppPublisherURL=https://www.beatfreakstudio.com
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
UninstallDisplayName={#MyName} (VST3)
OutputBaseFilename=PRIMEQpro-{#MyVersion}-Windows-Setup
OutputDir={#OUTDIR}
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Messages]
WelcomeLabel2=Este assistente vai instalar o {#MyName} (VST3, 64 bits) no teu sistema.%n%nProfessional Audio Plugin | Beatfreak Music%nParametric + Dynamic EQ%nCriador e Desenvolvedor: {#MyPublisher}%nVersao {#MyVersion}
FinishedLabel=O {#MyName} foi instalado na pasta VST3 do sistema. Reinicie o seu DAW e faca um novo scan de plugins.%n%nBEATFREAK MUSIC - Professional Audio Technology%nwww.beatfreakstudio.com

[Files]
Source: "{#VST3SRC}\*"; DestDir: "{commoncf64}\VST3\PRIMEQ pro by Piradex.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\PRIMEQ pro by Piradex.vst3"
