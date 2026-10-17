<#
.SYNOPSIS
    Builds, stages, and packages the Djehuti Electronics Workbench Inno Setup installer.

.DESCRIPTION
    Reproducible packaging script for Djehuti Electronics Workbench.
    - Optionally builds the Release application payload (single-core MSBuild / CMake).
    - Stages runtime files, the Release VST3 runtime template and license notices into an
      explicit staging directory, failing if any required payload is missing.
    - Compiles the branded Inno Setup installer using ISCC.exe.
    - Reports the resulting installer path and signature status.

.PARAMETER SkipBuild
    Skip building the Release executable if already present.

.PARAMETER PayloadDir
    Custom path containing the pre-built application executable (defaults to bin/Release).

.PARAMETER CleanStage
    Clean the staging directory prior to assembling files.

.PARAMETER Sign
    Reserved. Code signing is not implemented in this script and no certificate is
    configured, so the installer is always produced and reported as unsigned.

.PARAMETER ISCCPath
    Explicit path to Inno Setup command-line compiler (ISCC.exe).

.EXAMPLE
    # Build payload, stage files, and compile installer:
    .\package.ps1

.EXAMPLE
    # Re-package existing build without rebuilding:
    .\package.ps1 -SkipBuild
#>

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [string]$PayloadDir = "",
    [switch]$CleanStage,
    [switch]$Sign,
    [string]$ISCCPath = ""
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$prototypeDir = Split-Path -Parent $scriptDir
$rootDir = Split-Path -Parent (Split-Path -Parent $prototypeDir)
$stageDir = Join-Path $scriptDir "stage"
$distDir = Join-Path $scriptDir "dist"
$issFile = Join-Path $scriptDir "DjehutiElectronicsWorkbench.iss"

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host " Djehuti Electronics Workbench (DEWB) Installer Packaging" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "Prototype Directory: $prototypeDir"
Write-Host "Installer Directory: $scriptDir"

# 1. Locate ISCC.exe
if (-not $ISCCPath) {
    $searchPaths = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    )
    foreach ($candidate in $searchPaths) {
        if (Test-Path $candidate) {
            $ISCCPath = $candidate
            break
        }
    }
    if (-not $ISCCPath) {
        $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
        if ($cmd) {
            $ISCCPath = $cmd.Source
        }
    }
}

if (-not $ISCCPath -or -not (Test-Path $ISCCPath)) {
    Write-Error "Inno Setup Compiler (ISCC.exe) was not found. Please install Inno Setup 6 or 7 or provide -ISCCPath."
    exit 1
}
Write-Host "Inno Setup Compiler: $ISCCPath" -ForegroundColor Green

# 2. Derive Application Version from CMakeLists.txt
$cmakeFile = Join-Path $prototypeDir "CMakeLists.txt"
$version = "0.1.0"
if (Test-Path $cmakeFile) {
    $match = Select-String -Path $cmakeFile -Pattern 'project\s*\(\s*DjehutiElectronicsLab\s+VERSION\s+([0-9\.]+)'
    if ($match -and $match.Matches.Groups.Count -gt 1) {
        $version = $match.Matches.Groups[1].Value.Trim()
    }
}
Write-Host "Derived Application Version: $version" -ForegroundColor Green

# 3. Build or Accept Release Application Payload
if (-not $PayloadDir) {
    $PayloadDir = Join-Path $prototypeDir "bin\Release"
}

$exeName = "djehuti_electronics_lab.exe"
$exePath = Join-Path $PayloadDir $exeName

if (-not $SkipBuild) {
    Write-Host "`nBuilding Release payload (strictly single-core /m:1)..." -ForegroundColor Yellow
    $buildDir = Join-Path $prototypeDir "build"
    if (-not (Test-Path $buildDir)) {
        Write-Error "Build directory $buildDir does not exist. Configure CMake before packaging."
        exit 1
    }

    # Run single-core Release build
    $cmakeCmd = "cmake --build `"$buildDir`" --config Release --target DjehutiElectronicsLab --parallel 1"
    Write-Host "Executing: $cmakeCmd"
    cmd /c $cmakeCmd
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Release build failed with exit code $LASTEXITCODE."
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path $exePath)) {
    Write-Error "Executable not found at expected payload path: $exePath"
    exit 1
}
$exeSizeMb = [math]::Round(((Get-Item $exePath).Length / 1MB), 2)
Write-Host "Release Executable Verified: $exePath ($exeSizeMb MB)" -ForegroundColor Green

# 4. Ensure Branding Assets are Generated
$assetsDir = Join-Path $scriptDir "assets"
$appIco = Join-Path $assetsDir "app.ico"
$wizImg = Join-Path $assetsDir "wizard_image.bmp"
$wizSmall = Join-Path $assetsDir "wizard_small.bmp"

if (-not (Test-Path $appIco) -or -not (Test-Path $wizImg) -or -not (Test-Path $wizSmall)) {
    Write-Host "`nGenerating installer artwork and icons..." -ForegroundColor Yellow
    $genScript = Join-Path $scriptDir "scripts\generate_assets.ps1"
    powershell -ExecutionPolicy Bypass -File $genScript
}

# 5. Assemble Explicit Staging Manifest
Write-Host "`nAssembling explicit staging manifest in $stageDir..." -ForegroundColor Yellow
if ($CleanStage -and (Test-Path $stageDir)) {
    Remove-Item -Path $stageDir -Recurse -Force
}
if (-not (Test-Path $stageDir)) {
    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
}

# Every staged payload below is required. A missing file stops packaging instead of
# producing an installer that silently lacks it.
function Copy-RequiredFile([string]$Source, [string]$Destination, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        Write-Error "Required payload missing: $Description (expected at $Source)."
        exit 1
    }
    $destDir = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

# 5a. Executable
Copy-RequiredFile $exePath (Join-Path $stageDir $exeName) "Release application executable"

# 5b. WebView2Loader.dll
$wv2Source = Join-Path $PayloadDir "WebView2Loader.dll"
if (-not (Test-Path $wv2Source)) {
    $wv2Source = Join-Path $prototypeDir "third_party\webview2\x64\WebView2Loader.dll"
}
Copy-RequiredFile $wv2Source (Join-Path $stageDir "WebView2Loader.dll") "WebView2Loader.dll"
Write-Host "Staged: WebView2Loader.dll"

# 5c. default_layouts.json
$layoutsSource = Join-Path $PayloadDir "default_layouts.json"
if (-not (Test-Path $layoutsSource)) {
    $layoutsSource = Join-Path $prototypeDir "Resources\default_layouts.json"
}
Copy-RequiredFile $layoutsSource (Join-Path $stageDir "default_layouts.json") "default_layouts.json"
Write-Host "Staged: default_layouts.json"

# 5d. MSVC 143 CRT Redistributable DLLs (app-local deployment; all are required)
$msvcRedistDir = ""
$redistSearchPaths = @(
    "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.42.34433\x64\Microsoft.VC143.CRT",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT"
)
foreach ($pattern in $redistSearchPaths) {
    $found = Get-Item -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $msvcRedistDir = $found.FullName
        break
    }
}
if (-not $msvcRedistDir) {
    Write-Error "Visual C++ 2022 x64 CRT redistributable folder (Microsoft.VC143.CRT) was not found. Install the Visual Studio C++ redistributable components."
    exit 1
}
$crtDlls = @("concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140_threads.dll")
foreach ($dll in $crtDlls) {
    Copy-RequiredFile (Join-Path $msvcRedistDir $dll) (Join-Path $stageDir $dll) "Visual C++ runtime $dll"
}
Write-Host "Staged: MSVC CRT Redistributable DLLs from $msvcRedistDir"

# 5e. Runtime data folder (component_seed.json, models)
$stageData = Join-Path $stageDir "data"
if (Test-Path $stageData) { Remove-Item -Path $stageData -Recurse -Force }
New-Item -ItemType Directory -Path $stageData -Force | Out-Null
$srcData = Join-Path $prototypeDir "data"
Copy-RequiredFile (Join-Path $srcData "component_seed.json") (Join-Path $stageData "component_seed.json") "data\component_seed.json"
$srcModels = Join-Path $srcData "models"
if (-not (Test-Path $srcModels)) {
    Write-Error "Required payload missing: data\models (expected at $srcModels)."
    exit 1
}
Copy-Item -Path $srcModels -Destination (Join-Path $stageData "models") -Recurse -Force
Write-Host "Staged: data/ (component_seed.json, models)"

# 5f. Runtime knowledge folder (cookbook cards, taxonomy, acceptance goals)
$stageKnowledge = Join-Path $stageDir "knowledge"
if (Test-Path $stageKnowledge) { Remove-Item -Path $stageKnowledge -Recurse -Force }
New-Item -ItemType Directory -Path $stageKnowledge -Force | Out-Null
$srcKnowledge = Join-Path $prototypeDir "knowledge"
if (-not (Test-Path (Join-Path $srcKnowledge "cards"))) {
    Write-Error "Required payload missing: knowledge\cards (expected under $srcKnowledge)."
    exit 1
}
Copy-Item -Path (Join-Path $srcKnowledge "*") -Destination $stageKnowledge -Recurse -Force
Write-Host "Staged: knowledge/ (cards, cookbook taxonomy, goals)"

# 5g. Examples and sample projects for end users
$stageExamples = Join-Path $stageDir "examples"
if (Test-Path $stageExamples) { Remove-Item -Path $stageExamples -Recurse -Force }
New-Item -ItemType Directory -Path $stageExamples -Force | Out-Null
$srcExamples = Join-Path $prototypeDir "examples"
if (Test-Path $srcExamples) {
    Copy-Item -Path (Join-Path $srcExamples "*") -Destination $stageExamples -Recurse -Force
    Write-Host "Staged: examples/ (sample projects)"
}

# 5h. Release VST3 runtime template: the exact known Release output, never Debug and never a search.
#     Build it with: cmake --build build --config Release --target DjehutiCircuitVst_VST3 --parallel 1
$vstBundleName = "Djehuti Circuit VST.vst3"
$vstReleaseBinary = Join-Path $prototypeDir "build\DjehutiCircuitVst_artefacts\Release\VST3\$vstBundleName\Contents\x86_64-win\$vstBundleName"
$identityHeader = Join-Path $prototypeDir "build\GeneratedVst\GeneratedPluginIdentity.h"
# The template must carry the build-owned default identity, not one left behind by a developer export.
if (-not (Test-Path -LiteralPath $identityHeader) -or -not (Select-String -LiteralPath $identityHeader -SimpleMatch -Quiet -Pattern '"com.djehuti.circuitvst"')) {
    Write-Error "build\GeneratedVst\GeneratedPluginIdentity.h does not hold the default template identity (com.djehuti.circuitvst). Restore it and rebuild the Release VST3 template before packaging."
    exit 1
}
if ((Test-Path -LiteralPath $vstReleaseBinary) -and ((Get-Item -LiteralPath $identityHeader).LastWriteTime -gt (Get-Item -LiteralPath $vstReleaseBinary).LastWriteTime)) {
    Write-Error "GeneratedPluginIdentity.h is newer than the Release VST3 template. Rebuild the Release DjehutiCircuitVst_VST3 target before packaging."
    exit 1
}
$stageTemplates = Join-Path $stageDir "templates"
if (Test-Path $stageTemplates) { Remove-Item -Path $stageTemplates -Recurse -Force }
$stagedVstBinary = Join-Path $stageTemplates "vst3\$vstBundleName\Contents\x86_64-win\$vstBundleName"
Copy-RequiredFile $vstReleaseBinary $stagedVstBinary "Release VST3 runtime template (build the DjehutiCircuitVst_VST3 target in Release)"
Write-Host "Staged: templates/vst3/$vstBundleName (Release)"

# 5i. Application license and third-party notices (authoritative texts, copied unchanged)
$stageLicenses = Join-Path $stageDir "licenses"
if (Test-Path $stageLicenses) { Remove-Item -Path $stageLicenses -Recurse -Force }
$juceRoot = "D:\JUCE"
$licenseFiles = [ordered]@{
    "THIRD_PARTY_NOTICES.md"    = (Join-Path $scriptDir "THIRD_PARTY_NOTICES.md")
    "LICENSE.txt"               = (Join-Path (Split-Path -Parent $prototypeDir) "LICENSE")
    "JUCE-LICENSE.md"           = (Join-Path $juceRoot "LICENSE.md")
    "VST3_SDK-LICENSE.txt"      = (Join-Path $juceRoot "modules\juce_audio_processors\format_types\VST3_SDK\LICENSE.txt")
    "libpng-LICENSE.txt"        = (Join-Path $juceRoot "modules\juce_graphics\image_formats\pnglib\LICENSE")
    "LLVM-LICENSE.txt"          = "D:\000 Creation Suite\apps\CreationEngine\vcpkg_installed\x64-windows\share\llvm\copyright"
    "libavoid-LICENSE.LGPL.txt" = (Join-Path $prototypeDir "third_party\libavoid\LICENSE.LGPL")
    "libavoid-README.txt"       = (Join-Path $prototypeDir "third_party\libavoid\README")
    "FrustLang-LICENSE.txt"     = "D:\FrustLang\LICENSE"
    "DjehutiRoute-LICENSE.txt"  = "D:\DjehutiRoute\LICENSE"
    "WebView2-LICENSE.txt"      = (Join-Path $env:USERPROFILE ".nuget\packages\microsoft.web.webview2\1.0.4258.31\LICENSE.txt")
}
foreach ($entry in $licenseFiles.GetEnumerator()) {
    Copy-RequiredFile $entry.Value (Join-Path $stageLicenses $entry.Key) "license file $($entry.Key)"
}
Write-Host "Staged: licenses/ ($($licenseFiles.Count) files)"

# 6. Compile Inno Setup Installer
Write-Host "`nCompiling Inno Setup installer..." -ForegroundColor Yellow
if (-not (Test-Path $distDir)) {
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null
}

$isccArgs = @(
    "/DMyAppVersion=$version",
    "/DMyAppExeName=$exeName",
    "/DMyStageDir=$stageDir",
    "/DMyOutputDir=$distDir",
    "`"$issFile`""
)

$isccCmd = "`"$ISCCPath`" $($isccArgs -join ' ')"
Write-Host "Executing: $isccCmd"
& "$ISCCPath" $isccArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "ISCC compiler failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

# 7. Code Signing Status
Write-Host "`nCode Signing Status:" -ForegroundColor Cyan
if ($Sign) {
    Write-Host "Signing requested, but signing is not implemented in this script." -ForegroundColor Yellow
    Write-Host "Result: Unsigned." -ForegroundColor Yellow
} else {
    Write-Host "Result: Unsigned (signing is not implemented; no certificate is configured)." -ForegroundColor Yellow
}

# 8. Report Resulting Installer Path
$expectedInstaller = Join-Path $distDir "Djehuti_Electronics_Workbench_Setup_v$version.exe"
if (Test-Path $expectedInstaller) {
    $instSizeMb = [math]::Round(((Get-Item $expectedInstaller).Length / 1MB), 2)
    Write-Host "`n========================================================" -ForegroundColor Green
    Write-Host " SUCCESSFUL INSTALLER BUILD" -ForegroundColor Green
    Write-Host "========================================================" -ForegroundColor Green
    Write-Host "Installer Executable: $expectedInstaller" -ForegroundColor Green
    Write-Host "Installer Size:       $instSizeMb MB" -ForegroundColor Green
    Write-Host "Application Version:  $version" -ForegroundColor Green
    Write-Host "Staging Directory:    $stageDir" -ForegroundColor Green
    Write-Host "========================================================`n" -ForegroundColor Green
} else {
    Write-Error "Installer executable was not found at $expectedInstaller"
    exit 1
}
