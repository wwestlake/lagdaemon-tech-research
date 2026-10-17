# Djehuti Electronics Workbench — License and Third-Party Notices

This file is installed with Djehuti Electronics Workbench in `licenses\`. The
full license texts listed below are copied unchanged from their authoritative
sources by `installer\package.ps1`, which stops if any of them is missing.

## Application

| Item | License | Installed file |
| --- | --- | --- |
| Djehuti Electronics Workbench | GNU General Public License v3 | `licenses\LICENSE.txt` (from `Electronics Design Tool\LICENSE`) |

The application license file is the plain GPL-3.0 text. It does not carry a
copyright line naming the application's copyright holder; that statement is
not supplied here because no authoritative one exists in the repository.

## Components distributed in this installer

| Component | Where it is used | License | Installed file |
| --- | --- | --- | --- |
| JUCE 7.0.9 (Raw Material Software Limited) | Application and VST3 runtime template | JUCE 7 licence, used here under GPL-3.0; juce_audio_basics, juce_audio_devices, juce_core and juce_events are ISC | `licenses\JUCE-LICENSE.md` |
| Steinberg VST3 SDK (bundled in JUCE) | VST3 runtime template | Steinberg VST3 License or GPL-3.0 (dual) | `licenses\VST3_SDK-LICENSE.txt` |
| libpng (bundled in JUCE) | Application and template image support | libpng license | `licenses\libpng-LICENSE.txt` |
| LLVM | Embedded Frust compiler / JIT (statically linked) | Apache License 2.0 with LLVM Exceptions | `licenses\LLVM-LICENSE.txt` |
| libavoid (Monash University) | Schematic connector routing (statically linked) | GNU LGPL v2.1 | `licenses\libavoid-LICENSE.LGPL.txt`, `licenses\libavoid-README.txt` |
| FrustLang libraries (Frust compiler, runtime, plugin host, CreationDock, AI provider) | Application | MIT, Copyright (c) 2026 William Westlake | `licenses\FrustLang-LICENSE.txt` |
| DjehutiRoute | PCB routing and board outlines | MIT, Copyright (c) 2026 William Westlake | `licenses\DjehutiRoute-LICENSE.txt` |
| Microsoft Edge WebView2 loader (`WebView2Loader.dll`, NuGet Microsoft.Web.WebView2 1.0.4258.31) | Documentation viewer | Microsoft BSD-style license | `licenses\WebView2-LICENSE.txt` |
| Microsoft Visual C++ 2015–2022 runtime DLLs (`msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll`) | Application, installed app-locally | Visual Studio 2022 license terms, "Distributable Code" | See gap below |

Code compiled in through JUCE that carries notices in its source headers:

- zlib — Copyright (C) 1995-2005 Jean-loup Gailly and Mark Adler (zlib license).
- IJG JPEG library — This software is based in part on the work of the
  Independent JPEG Group.

## Known gap

- **Microsoft Visual C++ runtime:** these DLLs are redistributed under the
  "Distributable Code" terms of the Microsoft Visual Studio 2022 license. No
  copy of that license text exists on the build machine (the Visual Studio
  `Licenses` folder holds only Windows SDK licenses), so it is not installed.
  The terms are published at https://visualstudio.microsoft.com/license-terms/.
