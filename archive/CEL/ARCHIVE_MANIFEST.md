# CEL Archive Manifest

Archived on 2026-08-27 as part of the Creation Suite CEL retirement.

## Source Origins

| Archive path | Original repository path | Source commit |
| --- | --- | --- |
| `shared-CEL/` | `CreationSuite-Codex/shared/CEL/` | `30bf9f81e42d1db1d9a629507a4cfb72861e5c40` |
| `creation-engine/Language/` | `CreationEngine/Language/` | `33b4560f1f9b852eb1fe0c0ca467fba2c58ff40b` |
| `creation-engine/cmake/CelGrammar.cmake` | `CreationEngine/cmake/CelGrammar.cmake` | `33b4560f1f9b852eb1fe0c0ca467fba2c58ff40b` |
| `creation-station/Source/Language/` | `CreationStation/Source/Language/` | `121023d97eabfcbb4e0c2278b5050f2b23d7285b` |
| `creation-station/Source/Views/` | `CreationStation/Source/Views/{DslPanel,FoleyPanel}.{h,cpp}` | `121023d97eabfcbb4e0c2278b5050f2b23d7285b` |

## Deliberate Exclusions

- `shared/NodeSystem` remains in the Creation Suite. It is language-neutral
  graph, pin, validation, serialization, and editor infrastructure used by
  non-CEL systems.
- General application files that merely called a CEL surface remain in their
  original repositories until their FRust replacements are implemented.
- Build artifacts and `.git` directories were not copied.

## Retirement Rule

CEL must not be reintroduced as a production dependency. New language-hosting,
automation, graph-generation, and plugin work belongs to FRust. This archive
may be built or modified only as research work, outside the Creation Suite
production build graph.
