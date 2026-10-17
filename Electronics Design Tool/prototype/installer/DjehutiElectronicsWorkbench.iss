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

; Visual C++ 2015-2022 x64 runtime DLLs (app-local deployment; package.ps1 requires every one)
Source: "{#MyStageDir}\concrt140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\msvcp140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\msvcp140_2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\msvcp140_atomic_wait.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\msvcp140_codecvt_ids.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyStageDir}\vcruntime140_threads.dll"; DestDir: "{app}"; Flags: ignoreversion

; Runtime component seed and SPICE models
Source: "{#MyStageDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

; Knowledge base, cookbook recipes, taxonomy and tool cards
Source: "{#MyStageDir}\knowledge\*"; DestDir: "{app}\knowledge"; Flags: ignoreversion recursesubdirs createallsubdirs

; Sample projects and demonstrations
Source: "{#MyStageDir}\examples\*"; DestDir: "{app}\examples"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Release VST3 runtime template used by installed VST3 export (required)
Source: "{#MyStageDir}\templates\vst3\Djehuti Circuit VST.vst3\Contents\x86_64-win\Djehuti Circuit VST.vst3"; DestDir: "{app}\templates\vst3\Djehuti Circuit VST.vst3\Contents\x86_64-win"; Flags: ignoreversion

; Application license and third-party notices (required)
Source: "{#MyStageDir}\licenses\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\JUCE-LICENSE.md"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\VST3_SDK-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\libpng-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\LLVM-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\libavoid-LICENSE.LGPL.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\libavoid-README.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\FrustLang-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\DjehutiRoute-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#MyStageDir}\licenses\WebView2-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

; Application branding icon
Source: "assets\app.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Djehuti Electronics Workbench"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\License and Third-Party Notices"; Filename: "{app}\licenses\THIRD_PARTY_NOTICES.md"
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

// Helper: Detect a system-wide Visual C++ 2015-2022 x64 runtime. The application itself does not
// need it (its runtime DLLs are installed app-locally); VST3 plugins exported from the application
// are loaded by an audio host process, which must find the runtime on that computer.
function IsVCRedistInstalled(): Boolean;
var
  Installed: Cardinal;
begin
  Result := False;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1) then
  begin
    Result := True;
    Exit;
  end;
  if RegQueryDWordValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1) then
    Result := True;
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
    if not WizardSilent() and not IsVCRedistInstalled() then
    begin
      if MsgBox('The Microsoft Visual C++ 2015-2022 x64 runtime was not found on this computer.' + #13#10#13#10 +
                'Djehuti Electronics Workbench installs its own copy and does not need it. VST3 plugins you export, ' +
                'however, run inside your audio host and need this runtime on the computer where the host runs.' + #13#10#13#10 +
                'Would you like to open the official Microsoft download page now?',
                mbInformation, MB_YESNO) = IDYES then
      begin
        ShellExec('open', 'https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
      end;
    end;
  end;
end;
