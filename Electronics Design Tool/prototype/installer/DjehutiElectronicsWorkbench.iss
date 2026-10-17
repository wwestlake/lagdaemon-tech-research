; Script generated for Djehuti Electronics Workbench (DEWB)
; Official Inno Setup packaging script

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif

#ifndef MyAppExeName
  #define MyAppExeName "djehuti_electronics_lab.exe"
#endif

#ifndef MyStageDir
  #define MyStageDir "stage"
#endif

#ifndef MyOutputDir
  #define MyOutputDir "dist"
#endif

[Setup]
AppId={{8B1D7F2A-3C94-4E58-A591-6284C8F0E1DE}}
AppName=Djehuti Electronics Workbench
AppVersion={#MyAppVersion}
AppVerName=Djehuti Electronics Workbench {#MyAppVersion}
AppPublisher=Djehuti Project
AppPublisherURL=https://lagdaemon.com/electronics-work-bench/docs
AppSupportURL=https://lagdaemon.com/electronics-work-bench/docs
AppUpdatesURL=https://lagdaemon.com/electronics-work-bench/docs
DefaultDirName={autopf}\Djehuti Electronics Workbench
DefaultGroupName=Djehuti Electronics Workbench
AllowNoIcons=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
OutputBaseFilename=Djehuti_Electronics_Workbench_Setup_v{#MyAppVersion}
OutputDir={#MyOutputDir}
SetupIconFile=assets\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName=Djehuti Electronics Workbench
WizardStyle=modern
WizardImageFile=assets\wizard_image.bmp
WizardSmallImageFile=assets\wizard_small.bmp
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=auto
UsedUserAreasWarning=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main application executable and required runtime binaries
Source: "{#MyStageDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\default_layouts.json"; DestDir: "{app}"; Flags: ignoreversion

; Visual C++ Redistributable runtime DLLs
Source: "{#MyStageDir}\concrt140.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyStageDir}\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyStageDir}\msvcp140_1.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyStageDir}\msvcp140_2.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyStageDir}\msvcp140_atomic_wait.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyStageDir}\msvcp140_codecvt_ids.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyStageDir}\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyStageDir}\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MyStageDir}\vcruntime140_threads.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Runtime component seed and SPICE models
Source: "{#MyStageDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Knowledge base, cookbook recipes, taxonomy and tool cards
Source: "{#MyStageDir}\knowledge\*"; DestDir: "{app}\knowledge"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Sample projects and demonstrations
Source: "{#MyStageDir}\examples\*"; DestDir: "{app}\examples"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; VST3 export runtime templates (if staged)
Source: "{#MyStageDir}\templates\*"; DestDir: "{app}\templates"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Application branding icon
Source: "assets\app.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Djehuti Electronics Workbench"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\Djehuti Documentation"; Filename: "https://lagdaemon.com/electronics-work-bench/docs"
Name: "{group}\{cm:UninstallProgram,Djehuti Electronics Workbench}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Djehuti Electronics Workbench"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,Djehuti Electronics Workbench}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\{#MyAppExeName}.WebView2"

[Code]
// Helper: Detect Microsoft Edge WebView2 Runtime
function IsWebView2RuntimeInstalled(): Boolean;
var
  Version: String;
begin
  Result := False;
  // 1. WOW6432Node HKLM
  if RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) then
  begin
    if (Version <> '') and (Version <> '0.0.0.0') then
    begin
      Result := True;
      Exit;
    end;
  end;
  // 2. Native 64-bit HKLM
  if RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) then
  begin
    if (Version <> '') and (Version <> '0.0.0.0') then
    begin
      Result := True;
      Exit;
    end;
  end;
  // 3. User HKCU
  if RegQueryStringValue(HKCU, 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Version) then
  begin
    if (Version <> '') and (Version <> '0.0.0.0') then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

// Helper: Detect Visual C++ 2015-2022 Redistributable x64
function IsVCRedistInstalled(): Boolean;
var
  Installed: Cardinal;
begin
  Result := False;
  if RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) then
  begin
    Result := (Installed = 1);
  end;
end;

function NextButtonClick(CurPageId: Integer): Boolean;
var
  ErrorCode: Integer;
begin
  Result := True;
  if CurPageId = wpReady then
  begin
    if not WizardSilent() and not IsWebView2RuntimeInstalled() then
    begin
      if MsgBox('Microsoft Edge WebView2 Runtime is recommended for the integrated documentation viewer.' + #13#10#13#10 +
                'Would you like to open the official Microsoft download page now?',
                mbConfirmation, MB_YESNO) = IDYES then
      begin
        ShellExec('open', 'https://go.microsoft.com/fwlink/p/?LinkId=2124703', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
      end;
    end;
  end;
end;
