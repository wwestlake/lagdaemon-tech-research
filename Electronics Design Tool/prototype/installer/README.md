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
├── THIRD_PARTY_NOTICES.md    # License and notice summary (installed in licenses\)
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

## Release VST3 Runtime Template

Installed copies export VST3 plugins from a precompiled **Release** template instead of
building one. Build it once (single core) before packaging, and again whenever the plugin
runtime sources change:

```powershell
cd "D:\000 Tech Research\Electronics Design Tool\prototype"
cmake --build build --config Release --target DjehutiCircuitVst_VST3 --parallel 1
```

`package.ps1` stages exactly
`build\DjehutiCircuitVst_artefacts\Release\VST3\Djehuti Circuit VST.vst3\Contents\x86_64-win\Djehuti Circuit VST.vst3`.
It never uses a Debug build or searches for another plugin, and it stops if that file is
missing, if `build\GeneratedVst\GeneratedPluginIdentity.h` no longer holds the default
template identity (`com.djehuti.circuitvst`), or if that header is newer than the template.

## Staging Manifest

Every item below is required; `package.ps1` stops with an error naming the missing file
instead of producing an incomplete installer. Staging and output folders (`stage\`, `dist\`)
are ignored by Git.

1. **Application executable**: `djehuti_electronics_lab.exe` (Release, x64)
2. **WebView2 loader**: `WebView2Loader.dll`
3. **Layout configuration**: `default_layouts.json`
4. **Visual C++ 2015–2022 x64 runtime (app-local)**: `concrt140.dll`, `msvcp140*.dll`, `vcruntime140*.dll`
5. **Component libraries**: `data/component_seed.json`, `data/models/*`
6. **Knowledge base**: `knowledge/` (cards, taxonomy, acceptance goals)
7. **Examples**: `examples/*` (staged when present)
8. **VST3 runtime template**: `templates/vst3/Djehuti Circuit VST.vst3/Contents/x86_64-win/Djehuti Circuit VST.vst3` (Release)
9. **Licenses and notices**: `licenses/` — see `THIRD_PARTY_NOTICES.md` (installed as
   `licenses\THIRD_PARTY_NOTICES.md`, with a Start menu shortcut) for the application
   license, each bundled component's license file, and the one known gap (the Visual C++
   runtime license text is not available on the build machine).
10. **Branding**: `assets/app.ico`, wizard graphics

## VST3 Export in an Installed Copy

- The exporter uses the packaged Release template next to the executable and writes the
  current circuit artifact (`model.frust-il`), controls (`ui.json`) and `manifest.json` into
  the exported bundle's `Contents/Resources`, which the plugin loads at start-up.
- Export output goes to the open project's outputs folder or
  `%APPDATA%\DjehutiElectronicsLab\scratch_outputs\vst3_export`.
- An installed copy never discovers or rebuilds a development checkout. If the template is
  missing, export fails with a message asking for a repair or reinstall.
- Running from a development checkout (`prototype\bin\...`), or passing an explicit CMake
  build directory, keeps the developer workflow: the `DjehutiCircuitVst_VST3` target is
  rebuilt (Debug, `/m:1`) with the requested identity.
- **Limitation:** the template's compiled VST3 class identity (name "Djehuti Circuit VST",
  plugin code `DjCv` and its component UID) cannot change without recompiling. Template-based
  exports carry the requested product name, controls, effects and state in their resources,
  but hosts see every one of them as the same plugin class. The export result reports this
  as `identityLimitation`. Per-export identity requires the developer build workflow.
- Exported plugins need the Visual C++ 2015–2022 x64 runtime on the computer where the
  audio host runs; the app-local copy beside the application does not serve them.

## Prerequisites

- **Microsoft Edge WebView2 Runtime** (used by the documentation viewer): detected through
  the registry (`HKLM` 32/64-bit views and `HKCU`). If it is missing, an interactive install
  offers to open Microsoft's download page; nothing is downloaded or installed automatically,
  and silent installs skip the prompt.
- **Visual C++ 2015–2022 x64 runtime**: the application uses the app-local copies installed
  beside the executable. The installer also checks for a system-wide runtime
  (`HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64`, both registry views) and,
  when it is absent, tells the user that exported VST3 plugins need it and offers to open
  Microsoft's download page. Nothing is installed automatically.
- The installer has not been tested on a clean Windows machine; no clean-machine guarantee
  is made.

## Uninstall and User Data

Uninstall removes the installed application files (and the WebView2 cache folder the app
creates beside its executable). It does not touch `%APPDATA%\DjehutiElectronicsLab`
(settings, credentials, layouts, scratch outputs), user documents, projects or exported plugins.

## Code Signing

Code signing is not implemented. The installer and application are unsigned; `-Sign` only
reports that.

## Closeout Handoff (installer v0.1.0)

**Packaging commands used**

```powershell
cd "D:\000 Tech Research\Electronics Design Tool\prototype"
cmake --build build --config Release --target DjehutiCircuitVst_VST3 --parallel 1
cmake --build build --config Release --target DjehutiElectronicsLab --parallel 1
cd installer
.\package.ps1 -SkipBuild
```

Result: `installer\dist\Djehuti_Electronics_Workbench_Setup_v0.1.0.exe`, about 16.4 MB, unsigned.

**Verification actually performed**

- Silent per-user install into a separate test folder, then uninstall.
- The installed application started, answered as responsive and closed normally.
- One export through the installed application's local tool API (`/v1/tools/call`): a small
  Audio In → 4.7 kΩ / 22 nF / 100 kΩ → Audio Out circuit exported in `single` mode. The result
  reported `vst3BinarySource: packaged_release_template`; the bundle's binary was
  byte-identical to the installed Release template; `Contents/Resources` held the current
  circuit artifact, `ui.json` and `manifest.json`. The development build tree was not touched
  (no identity header change, no Debug plugin build).
- Uninstall removed the install folder, its uninstall registry entry and Start menu group;
  `%APPDATA%\DjehutiElectronicsLab` and the exported plugin remained.

**Not verified**

- The visible wizard pages, branding and the installed Help view were not inspected
  (artwork for review: `installer\assets\`).
- Loading an exported plugin in a VST3 host and audio processing were not checked.
- No clean-machine or VM test.

**Remaining limitations**

- Template-based exports share the template's VST3 class identity (see "VST3 Export in an
  Installed Copy").
- Exported plugins need the Visual C++ 2015–2022 x64 runtime where the host runs.
- The Visual C++ runtime license text is not packaged (not available on the build machine),
  and the application license file names no copyright holder.
