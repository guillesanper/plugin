; SPDX-License-Identifier: AGPL-3.0-or-later
;
; Instalador de esceQ para Windows (Inno Setup 6.3 o posterior).
;
; Instala:
;   VST3 en  C:\Program Files\Common Files\VST3\esceQ.vst3
;   CLAP en  C:\Program Files\Common Files\CLAP\esceQ.clap
; que son las carpetas donde FL Studio y los demás hosts buscan por defecto.
;
; El CI lo compila así (ver .github/workflows/ci.yml):
;   iscc /DAppVersion=0.1.0 /DSourceDir=<carpeta con VST3\ y CLAP\> packaging\windows\esceQ.iss
;
; Los textos que ve el usuario van en inglés.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\plugin\esceQ_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
; CONTRATO: AppId identifica la instalación. Si cambia, una versión nueva no
; actualiza la anterior, sino que se instala al lado. No tocarlo.
AppId={{8E0C2F5A-6B1D-4C3E-9A47-5D2E8F1B7C60}
AppName=esceQ
AppVersion={#AppVersion}
AppVerName=esceQ {#AppVersion}
AppPublisher=esceQ
AppPublisherURL=https://github.com/guillesanper/plugin
AppSupportURL=https://github.com/guillesanper/plugin/issues
AppUpdatesURL=https://github.com/guillesanper/plugin/releases
VersionInfoVersion={#AppVersion}
; No hay carpeta de aplicación: solo los plugins y, aparte, el desinstalador.
CreateAppDir=no
UninstallFilesDir={commonpf64}\esceQ
UninstallDisplayName=esceQ {#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=esceQ-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
DisableWelcomePage=no
; El host puede tener el plugin cargado: se avisa en vez de fallar a medias.
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This will install esceQ {#AppVersion}, a free parametric and dynamic equalizer, on your computer.%n%nPlease close FL Studio (or any other host) before continuing.

[Types]
Name: "full"; Description: "VST3 and CLAP"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 (recommended for FL Studio)"; Types: full custom
Name: "clap"; Description: "CLAP"; Types: full custom

[Files]
Source: "{#SourceDir}\VST3\esceQ.vst3\*"; DestDir: "{commoncf64}\VST3\esceQ.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\CLAP\esceQ.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{commonpf64}\esceQ"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\ui\assets\fonts\OFL.txt"; DestDir: "{commonpf64}\esceQ"; DestName: "Inter-font-license.txt"; Flags: ignoreversion

[UninstallDelete]
; El VST3 es una carpeta: se quita entera. Los presets de usuario
; (%APPDATA%\esceQ) no se tocan.
Type: filesandordirs; Name: "{commoncf64}\VST3\esceQ.vst3"
