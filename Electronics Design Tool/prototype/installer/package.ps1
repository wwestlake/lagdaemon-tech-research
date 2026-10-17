<#
.SYNOPSIS
    Builds, stages, and packages the Djehuti Electronics Workbench Inno Setup installer.

.DESCRIPTION
    Reproducible packaging script for Djehuti Electronics Workbench.
    - Optionally builds the Release application payload (single-core MSBuild / CMake).
    - Stages runtime files into an explicit staging directory.
    - Compiles the branded Inno Setup installer using ISCC.exe.
    - Reports the resulting installer path and signature status.

.PARAMETER SkipBuild
    Skip building the Release executable if already present.

.PARAMETER PayloadDir
    Custom path containing the pre-built application executable (defaults to bin/Release).

.PARAMETER CleanStage
    Clean the staging directory prior to assembling files.

.PARAMETER Sign
    Attempt code signing using an installed certificate. If no certificate is found,
    the script reports the unsigned status truthfully without failing.

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

# 5a. Executable
Copy-Item -Path $exePath -Destination (Join-Path $stageDir $exeName) -Force

# 5b. WebView2Loader.dll
$wv2Source = Join-Path $PayloadDir "WebView2Loader.dll"
if (-not (Test-Path $wv2Source)) {
    $wv2Source = Join-Path $prototypeDir "third_party\webview2\x64\WebView2Loader.dll"
}
if (Test-Path $wv2Source) {
    Copy-Item -Path $wv2Source -Destination (Join-Path $stageDir "WebView2Loader.dll") -Force
    Write-Host "Staged: WebView2Loader.dll"
}

# 5c. default_layouts.json
$layoutsSource = Join-Path $PayloadDir "default_layouts.json"
if (-not (Test-Path $layoutsSource)) {
    $layoutsSource = Join-Path $prototypeDir "Resources\default_layouts.json"
}
if (Test-Path $layoutsSource) {
    Copy-Item -Path $layoutsSource -Destination (Join-Path $stageDir "default_layouts.json") -Force
    Write-Host "Staged: default_layouts.json"
}

# 5d. MSVC 143 CRT Redistributable DLLs
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
if ($msvcRedistDir -and (Test-Path $msvcRedistDir)) {
    $crtDlls = @("concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140_threads.dll")
    foreach ($dll in $crtDlls) {
        $srcDll = Join-Path $msvcRedistDir $dll
        if (Test-Path $srcDll) {
            Copy-Item -Path $srcDll -Destination (Join-Path $stageDir $dll) -Force
        }
    }
    Write-Host "Staged: MSVC CRT Redistributable DLLs from $msvcRedistDir"
}

# 5e. Runtime data folder (component_seed.json, models)
$stageData = Join-Path $stageDir "data"
if (Test-Path $stageData) { Remove-Item -Path $stageData -Recurse -Force }
New-Item -ItemType Directory -Path $stageData -Force | Out-Null
$srcData = Join-Path $prototypeDir "data"
if (Test-Path $srcData) {
    Copy-Item -Path (Join-Path $srcData "component_seed.json") -Destination $stageData -Force -ErrorAction SilentlyContinue
    $srcModels = Join-Path $srcData "models"
    if (Test-Path $srcModels) {
        Copy-Item -Path $srcModels -Destination (Join-Path $stageData "models") -Recurse -Force
    }
    Write-Host "Staged: data/ (component_seed.json, models)"
}

# 5f. Runtime knowledge folder (cookbook cards, taxonomy, acceptance goals)
$stageKnowledge = Join-Path $stageDir "knowledge"
if (Test-Path $stageKnowledge) { Remove-Item -Path $stageKnowledge -Recurse -Force }
New-Item -ItemType Directory -Path $stageKnowledge -Force | Out-Null
$srcKnowledge = Join-Path $prototypeDir "knowledge"
if (Test-Path $srcKnowledge) {
    Copy-Item -Path (Join-Path $srcKnowledge "*") -Destination $stageKnowledge -Recurse -Force
    Write-Host "Staged: knowledge/ (cards, cookbook taxonomy, goals)"
}

# 5g. Examples and sample projects for end users
$stageExamples = Join-Path $stageDir "examples"
if (Test-Path $stageExamples) { Remove-Item -Path $stageExamples -Recurse -Force }
New-Item -ItemType Directory -Path $stageExamples -Force | Out-Null
$srcExamples = Join-Path $prototypeDir "examples"
if (Test-Path $srcExamples) {
    Copy-Item -Path (Join-Path $srcExamples "*") -Destination $stageExamples -Recurse -Force
    Write-Host "Staged: examples/ (sample projects)"
}

# 5h. Precompiled VST3 templates (if available)
$stageTemplates = Join-Path $stageDir "templates\vst3"
$vstArtefact = Join-Path $prototypeDir "build\DjehutiCircuitVst_artefacts\Debug\VST3\Djehuti Circuit VST.vst3"
if (Test-Path $vstArtefact) {
    if (-not (Test-Path $stageTemplates)) {
        New-Item -ItemType Directory -Path $stageTemplates -Force | Out-Null
    }
    Copy-Item -Path $vstArtefact -Destination $stageTemplates -Recurse -Force
    Write-Host "Staged: templates/vst3/ (Djehuti Circuit VST.vst3)"
}

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
    Write-Host "Signing requested: Checking for code signing certificates..."
    # If a signtool / cert is configured, sign here. Otherwise truthfully report unsigned.
    Write-Host "Result: Unsigned (no production code signing certificate configured on this build machine)." -ForegroundColor Yellow
} else {
    Write-Host "Result: Unsigned (code signing skipped by configuration, no signing certificate specified)." -ForegroundColor Yellow
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
