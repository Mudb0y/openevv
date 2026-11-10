; The SAPI 5 voice, packaged.
;
; Built with Inno Setup 6, on Windows, from what `make sapi5-stage' put in
; build\sapi5:
;
;     iscc /DAppVersion=0.7 sapi5\installer\openevv.iss
;
; It registers in HKEY_LOCAL_MACHINE, which is where SAPI programs look and
; the only place some older ones look, so it wants administrator. The
; settings a person changes while listening are in HKEY_CURRENT_USER, so the
; configurator never asks for anything.
;
; It installs both word sizes. A SAPI program can only load a voice of its
; own bitness, so on sixty-four bit Windows the voice goes in twice: once for
; Narrator and modern programs and once, under WOW64, for everything older.
; Each registers its tokens in its own view of the registry. On thirty-two bit
; Windows only the thirty-two bit voice goes in.

#ifndef AppVersion
#define AppVersion "0.0.0"
#endif
#define AppName "OpenEVV Voice"
#define AppURL "https://github.com/Mudb0y/openevv"
#define Stage "..\..\build\sapi5"

[Setup]
AppId={{48130469-4E25-4EC7-BA53-0EB5D491C6B4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=OpenEVV
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\OpenEVV Voice
DefaultGroupName={#AppName}
OutputDir=..\..\build
OutputBaseFilename=openevv-{#AppVersion}-sapi5-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile={#Stage}\LICENSE
; Sixty-four bit mode wherever the machine can run it, so that {app} is the
; real Program Files and the sixty-four bit voice can be registered.
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "shortcut"; Description: "Put {#AppName} Settings in the Start menu"; \
	GroupDescription: "Shortcuts:"

[Files]
; The engines first, where each voice looks for its own: eci-x86_64 and
; eci-x86 beside it. Registration asks the engine which languages it has,
; and Inno registers after every file is in place.
Source: "{#Stage}\eci-x86_64\eci.dll"; DestDir: "{app}\eci-x86_64"; \
	Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#Stage}\eci-x86\eci.dll"; DestDir: "{app}\eci-x86"; \
	Flags: ignoreversion

; The voices. regserver writes the tokens and the two classes, and
; uninstalling takes them out again. The thirty-two bit voice is registered
; by the thirty-two bit regsvr32, so what it writes lands in the WOW64 view,
; which is where thirty-two bit SAPI looks.
Source: "{#Stage}\openevv_sapi5.dll"; DestDir: "{app}"; \
	Flags: ignoreversion regserver 64bit; Check: Is64BitInstallMode
Source: "{#Stage}\openevv_sapi5_x86.dll"; DestDir: "{app}"; \
	Flags: ignoreversion regserver 32bit

; The configurator, in the machine's own word size. The settings live under
; HKEY_CURRENT_USER\Software, which both word sizes share, so one serves both.
Source: "{#Stage}\openevv_config.exe"; DestDir: "{app}"; \
	Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#Stage}\openevv_config32.exe"; DestDir: "{app}"; \
	DestName: "openevv_config.exe"; Flags: ignoreversion; \
	Check: not Is64BitInstallMode

Source: "{#Stage}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\NOTICE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName} Settings"; Filename: "{app}\openevv_config.exe"; \
	Tasks: shortcut
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"; \
	Tasks: shortcut

[Run]
Filename: "{app}\openevv_config.exe"; Description: "Open the settings now"; \
	Flags: postinstall nowait skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}\eci-x86_64"
Type: dirifempty; Name: "{app}\eci-x86"

[Code]
// The settings are left behind by an ordinary uninstall, so that somebody
// putting in a newer version does not have to set them again. Taking them
// away is offered instead -- through the suppressible box, so a silent
// uninstall answers No rather than waiting for somebody to.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    if RegKeyExists(HKEY_CURRENT_USER, 'Software\OpenEVV\SAPI5') then
      if SuppressibleMsgBox('Remove your OpenEVV voice settings as well?' + #13#10 +
                            'Choose No to keep them for a later installation.',
                            mbConfirmation, MB_YESNO, IDNO) = IDYES then
        RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\OpenEVV\SAPI5');
end;
