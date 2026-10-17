# Djehuti Electronics Workbench — Inno Setup Packaging

This directory contains the reproducible packaging scripts, installer definition, and branding assets for the **Djehuti Electronics Workbench (DEWB)** Windows installer.

## Overview

- **Installer Tool**: Inno Setup 7 (compatible with Inno Setup 6).
- **Target Application**: `djehuti_electronics_lab.exe` (Release configuration, x64).
- **AppId**: `{8B1D7F2A-3C94-4E58-A591-6284C8F0E1DE}` (stable GUID for smooth upgrades).
- **Default Installation Scope**: Per-user by default (`PrivilegesRequired=lowest`), installing to `{userpf}\Djehuti Electronics Workbench` (`%LOCALAPPDATA%\Programs\Djehuti Electronics Workbench`) without triggering elevation prompts, with administrative install options supported.
- **Data Protection**: User documents, projects, and application data stored under `%APPDATA%\DjehutiElectronicsLab` are preserved across uninstallation and upgrades.

## Directory Structure

```
installer/
├── assets/
│   ├── app.ico               # Multi-resolution application & installer icon
│   ├── wizard_image.bmp      # Left wizard banner artwork
│   └── wizard_small.bmp      # Top-right header logo
├── scripts/
│   └── generate_assets.ps1   # Asset generation script from vector/PNG source
├── DjehutiElectronicsWorkbench.iss # Inno Setup compiler script
├── package.ps1               # Master packaging PowerShell command
└── README.md                 # This documentation
```

## Quick Start: One-Command Packaging

To build the Release payload (strictly single-core), stage the required runtime files, and compile the installer:

```powershell
cd "D:\000 Tech Research\Electronics Design Tool\prototype\installer"
.\package.ps1
```

The resulting installer is placed in `installer\dist\`:
`Djehuti_Electronics_Workbench_Setup_v0.1.0.exe`

### Common Options

- **Skip compilation and package existing Release build**:
  ```powershell
  .\package.ps1 -SkipBuild
  ```

- **Clean the staging area before staging**:
  ```powershell
  .\package.ps1 -CleanStage
  ```

- **Specify custom pre-built payload location**:
  ```powershell
  .\package.ps1 -SkipBuild -PayloadDir "..\bin\Release"
  ```

- **Specify custom ISCC compiler path**:
  ```powershell
  .\package.ps1 -ISCCPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
  ```

## Staging Manifest

The packaging process explicitly selects only required runtime files:

1. **Application Executable**: `djehuti_electronics_lab.exe`
2. **WebView2 Support**: `WebView2Loader.dll`
3. **Layout Configurations**: `default_layouts.json`
4. **Microsoft Visual C++ Runtime**: `concrt140.dll`, `msvcp140*.dll`, `vcruntime140*.dll`
5. **Component Libraries**: `data/component_seed.json`, `data/models/*.lib`
6. **Knowledge Base**: `knowledge/cards/*.jsonl`, taxonomy, acceptance criteria
7. **Examples & Demonstrations**: `examples/*`
8. **Branding Assets**: `assets/app.ico`, wizard graphics

## Prerequisites & Detection

The installer automatically verifies:
- **Microsoft Edge WebView2 Runtime**: Checked via registry (`HKLM` and `HKCU`). If missing, prompts the user and offers to launch the official Microsoft installer.
- **Visual C++ 2015–2022 x64 Runtime**: Detected via `HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64`. Redistributable binaries are also bundled directly beside the executable to guarantee out-of-the-box execution on clean Windows machines.

## Code Signing

The package script reports code signing status truthfully. When no production code signing certificate is configured in the environment, the installer is built unsigned without generating invalid self-signed signatures.
