# Workspace Layouts & Presets Architecture

## Overview

Djehuti Electronics Lab provides a flexible, docking-based workspace layout architecture. All predefined workspace layouts (such as Default Layout, Schematic & Circuit Construction, Simulation & Analytics, Machine Builder & Systems, and FRust & Node Programming) are entirely data-driven via JSON.

Changes to preset names, panel assignments, tab orders, active tabs, and dock zone dimensions require **JSON edits only — no C++ compilation or relinking**.

---

## Storage Responsibilities

The layout system separates storage into three distinct files located in the per-user application data directory (`%APPDATA%\DjehutiElectronicsLab\`):

| File | Responsibility | Authoritative Source |
|---|---|---|
| `default_layouts.json` | Editable catalog of predefined workspace presets | User / Administrator / External Tools |
| `user_layouts.json` | Explicitly named layouts saved by the user via **Layout → Save Layout…** | User explicit actions |
| `layout.json` | Last-session workspace arrangement, restored on startup | Application runtime state |

### Shipped Template vs. Editable Catalog

- **Canonical Template**: Located at `Resources/default_layouts.json` in the repository and deployed alongside the binary (`bin/Debug/default_layouts.json`).
- **Editable Runtime Catalog**: Located at `%APPDATA%\DjehutiElectronicsLab\default_layouts.json`.
- **First Launch Seeding**: If the editable catalog does not exist in AppData, the application automatically seeds it from the shipped template.
- **Persistence**: Startup, normal operation, and builds **never** overwrite an existing `%APPDATA%\DjehutiElectronicsLab\default_layouts.json`, preserving user customizations.

---

## Dynamic Reloading (Zero Compilation)

The application monitors `default_layouts.json` on disk using file modification timestamps (`PredefinedLayoutCatalog::reloadIfChanged`):

- **Menu Construction**: Every time the **Layout** menu heading is opened in the GUI, the application checks if `default_layouts.json` has changed on disk. If so, it reloads the presets and refreshes the menu items immediately.
- **Applying Presets**: When selecting any preset or calling `applyPredefinedLayout`, the latest JSON definition is verified and loaded.
- **Agent API**: The in-app agent tools (`layout_list`, `layout_apply`) automatically trigger `reloadIfChanged`, ensuring automated workflows reflect disk edits immediately.
- **Binary Hash / Timestamps**: Because layout definitions are external JSON data, the executable binary (`djehuti_electronics_lab.exe`) remains unchanged and does not require relinking.

---

## Resilience & Error Handling

If a user edits `default_layouts.json` with syntax errors (e.g., malformed JSON) or semantic validation errors (e.g., duplicate IDs, duplicate panel assignments across zones, missing required zones):

1. The parser reports a descriptive diagnostic message to the application log (`appendLog`).
2. The catalog **retains the last known good configuration in memory**.
3. The catalog file on disk is left intact (never corrupted or wiped).
4. The application continues running without crashing.

---

## JSON Catalog Schema

```json
{
  "version": 1,
  "presets": [
    {
      "id": "default",
      "name": "Default Layout",
      "order": 0,
      "leftWidth": 260,
      "rightWidth": 280,
      "bottomHeight": 220,
      "zones": {
        "left": {
          "panels": ["library", "hierarchy"],
          "currentTab": 0
        },
        "center": {
          "panels": ["schematic", "analytics", "pcb", "help"],
          "currentTab": 3
        },
        "right": {
          "panels": ["properties", "agent"],
          "currentTab": 0
        },
        "bottom": {
          "panels": ["log", "console"],
          "currentTab": 0
        }
      },
      "floating": []
    }
  ]
}
```

### Zone & Panel Identifiers

- **Available Zones**: `left`, `center`, `right`, `bottom`.
- **Standard Panel Identifiers**:
  - `library`: Component and SPICE parts library.
  - `hierarchy`: Hierarchical schematic sheets navigator.
  - `schematic`: Schematic editor and circuit canvas.
  - `analytics`: Signal analytics, waveform plots, and instruments.
  - `pcb`: PCB layout editor.
  - `help`: Documentation and help system.
  - `control_panel`: System simulation controls and actuators.
  - `mechanical_view`: 3D mechanical and physical view.
  - `machine_builder`: Machine and electro-mechanical system designer.
  - `demo_runner`: Demonstration scenario runner.
  - `nodes`: FRust node-graph designer.
  - `frust`: FRust code editor and script runner.
  - `debugger`: FRust debugger and inspection.
  - `properties`: Component and object property inspector.
  - `agent`: In-app BYOK agent and assistant.
  - `spec_ingestion`: Datasheet and specification ingestion.
  - `sourcing`: Parts sourcing and BOM manager.
  - `log`: Application event log.
  - `console`: Scripting and execution console.

---

## Agent Parity

In accordance with Djehuti Electronics Lab agent parity rules:
- `layout_list`: Lists all available predefined catalog presets and user-saved layouts.
- `layout_apply`: Restores a predefined preset or saved user layout by ID or display name.
- `layout_save`: Saves the current workspace arrangement under a specified name in `user_layouts.json`.
