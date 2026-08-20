# Handoff Doc — JUCE Docking Framework Location

Written by Claude for Codex. Scoped to one question: where the custom
JUCE docking system lives and how the IDE uses it. Not a full project
dump — see `HANDOFF_GEMINI.md` at repo root for broader project context
if you need it (it's stale in places, but the repo/toolchain background
still holds).

## The library itself: `projects/03_creation_dock/`

A standalone static library (CMake target `CreationDock`), built from
scratch - **not** JUCE's own `TabbedComponent`/`DockingWindow` (JUCE
doesn't ship a real docking system). Resizable splitters, drag-to-dock
zones, persisted layout to disk.

```
projects/03_creation_dock/
  CMakeLists.txt
  include/CreationDock/
    DockManager.h        - the top-level API: registerPanel(), load/save layout
    DockContainer.h
    DockPanel.h
    DockTab.h
    DockTabStrip.h
    DockZone.h            - defines DockTargetZone (Left/Right/Bottom/CenterTab/...)
    DockOverlay.h          - the drag-and-drop drop-zone highlight overlay
    DockSplitter.h
    FloatingDockWindow.h
  src/
    (one .cpp per header above, same names)
```

`CMakeLists.txt` locates JUCE via `JUCE_PATH` (cache var or env var - no
hardcoded machine path), links `juce::juce_core`/`juce_graphics`/
`juce_gui_basics`/`juce_gui_extra`, and (Linux only) wires GTK3's
pkg-config include path - `juce_gui_extra.cpp` gets recompiled fresh for
*every* target that links it (JUCE has no shared runtime on Linux), so if
you ever add a new target that links `juce::juce_gui_extra` directly,
it'll need this same GTK3 wiring or it won't compile on Linux. See the
comments in this file's `CMakeLists.txt` for the full explanation - this
bit it a real, non-obvious multi-hour debugging session.

## Where the IDE actually uses it

`projects/02_juce_language_host/Source/WorkbenchComponent.h/.cpp` is the
integration point - the IDE's main window.

- `WorkbenchComponent.h:4` — `#include <CreationDock/DockManager.h>`
- `WorkbenchComponent.h:37` — `std::unique_ptr<CreationDock::DockManager> dockManager;`
- `WorkbenchComponent.cpp` (around line 61-68) — every panel the IDE has
  gets registered here, one line each:

  ```cpp
  dockManager->registerPanel("explorer", "Project Explorer", std::move(fileTree), CreationDock::DockTargetZone::Left);
  dockManager->registerPanel("editor", "Code Editor", std::move(editor), CreationDock::DockTargetZone::CenterTab);
  dockManager->registerPanel("metadata", "AST & Metadata Inspector", std::move(metadata), CreationDock::DockTargetZone::Right);
  dockManager->registerPanel("context", "Frust Context", std::move(context), CreationDock::DockTargetZone::Right);
  dockManager->registerPanel("frate", "Frate Package Manager", std::move(frate), CreationDock::DockTargetZone::Right);
  dockManager->registerPanel("ai", "AI Assistant", std::move(aiChat), CreationDock::DockTargetZone::Right);
  dockManager->registerPanel("console", "Console & Output REPL", std::move(console), CreationDock::DockTargetZone::Bottom);
  dockManager->registerPanel("terminal", "OS Terminal", std::move(terminal), CreationDock::DockTargetZone::Bottom);

  dockManager->loadLayoutFromFile(getLayoutFile());
  ```

  That's the whole pattern for adding a new dockable panel: build your
  `juce::Component`, `registerPanel(id, displayName, std::move(component), zone)`.

- Layout persistence: `dockManager->saveLayoutToFile(...)` /
  `loadLayoutFromFile(...)` - called on window resize/close and on
  startup. `getLayoutFile()` (in `WorkbenchComponent.cpp`) points at
  `%APPDATA%\LagDaemonResearchIDE\layout.json` on Windows (equivalent
  user-data dir on Linux via JUCE's own `getSpecialLocation` - same
  mechanism, no path hardcoded per-platform in this file itself).
- `dockManager->resetLayout()` - wired to a menu item, discards the saved
  layout and goes back to the `registerPanel()` calls' default zones.

## If you're extending it

- New panel = new `juce::Component` subclass + one `registerPanel()` call
  in `WorkbenchComponent.cpp`. Don't touch `DockManager` itself unless the
  panel needs a genuinely new *zone* concept, not just another panel.
- The library is consumed via relative path
  (`add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../03_creation_dock" ...)`)
  from three consumers right now: the IDE, and (transitively, since it's
  in the same build graph) nothing else currently links it directly -
  it's IDE-only, not part of `frust_lang`/`frust_compiler`/`frate`.
