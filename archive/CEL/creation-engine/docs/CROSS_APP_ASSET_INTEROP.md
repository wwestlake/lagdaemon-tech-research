# Cross-App Asset Interop Contract

This document defines the shared asset and CEL interop contract the
Creation suite should follow, with Creation Engine and Creation Station
as the first two implementation targets.

Creation Movie and Creation Live should adopt the same contract after
the Station/Engine integration path is stable.

## Scope

This contract covers:

- shared asset identity
- immutable asset versioning
- project VFS versus suite asset store responsibilities
- CEL portability rules
- graph-to-CEL relationship

It does not define final network transport or a final shared service
implementation.

## Storage Model

The suite uses two layers:

- suite asset store for canonical shared reusable assets
- project VFS for per-project documents, local state, references, and
  optional vendored copies

Projects are not the canonical shared library. They are documents that
refer to the canonical shared library.

## Shared Asset Identity

Each asset has:

- `asset_id`: stable conceptual identity
- `version_id`: immutable exact revision identity

If content or behavior-relevant metadata changes, a new version must be
created.

Existing versions are never mutated in place.

## Reference Semantics

Shared references may be:

- exact version
- compatible-latest
- latest

Exact version is required for deterministic builds and archival
reproducibility.

## Canonical Asset Record

Each asset version should record at minimum:

- `asset_id`
- `version_id`
- `kind`
- `logical_name`
- `content_hash`
- `media_type`
- `created_at`
- `created_by_app`
- `dependency_refs`
- `domain_tags`
- `metadata`

## CEL Portability

CEL is the shared executable language across the suite.

Two categories matter:

- core CEL with no host-specific calls
- host CEL with explicit domain-specific intrinsic usage

Core CEL must compile everywhere.

Host CEL must compile only where the app host policy allows its
required domains.

See also:

- [CROSS_APP_LANGUAGE_DOMAINS.md](./CROSS_APP_LANGUAGE_DOMAINS.md)

## CEL Asset Manifests

Every shared CEL asset should declare:

- required domains
- entry points
- exported symbols
- dependency refs
- ABI/runtime version
- language version

This is the shared capability manifest concept referenced by the
cross-app language domain work.

## Graph Relationship

Node graphs are authoring artifacts, not a separate final scripting
language.

The suite rule is:

- graphs generate CEL
- CEL is what the compiler and runtime execute
- graph diagnostics should map back from generated CEL where practical

That keeps one executable language family instead of one per app.

## Engine Responsibilities

Creation Engine already provides:

- the reference CEL compiler/JIT stack
- the reference domain-gating mechanism for CEL
- the initial zip-backed VFS direction

Creation Engine should remain the reference implementation for the CEL
core while Station converges on the same language system.

## Station Responsibilities

Creation Station should:

- replace its private language stack with the shared CEL path
- move project asset references away from loose filesystem paths
- adopt shared asset identity and immutable version semantics
- add Station-specific audio domains and intrinsics on top of CEL

## First Implementation Order

1. shared asset reference structure
2. Station project migration toward exact asset/version references
3. Station adoption of CEL core and node system
4. Station audio-domain intrinsic layer
5. broader suite asset catalog/index

## Non-Goals

This contract does not require:

- one repo for all apps
- one runtime binary for all apps
- immediate network asset service
- immediate Movie/Live feature development

The goal is stable interoperability between the two main apps first.
