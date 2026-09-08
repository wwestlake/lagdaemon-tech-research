# SongScape — Design Notes

*Working name for Harmonia's open-world layer: the ground/world generation and
survival-magic system replacing the removed Living Grid / voxel-CA mechanic.*

Status: **early brainstorm, not yet implemented.** Captured here so it
survives past chat. Nothing below is committed to code yet.

---

## Core pitch

**Magic is music. Music is magic.**

Not flavor text — the actual mechanic. Every spell, every fight, every
piece of the world works by real music-theory relationships, using the
music-theory code Harmonia already has (`MusicTheory.h`, `CircleOfFifths.h`).
A player who gets good at the game has learned something true about how
music actually works, without it ever feeling like a lesson.

Genre: **open-world survival**, explicitly *not* quest-driven or linear.
No forced order, no required chain of objectives. Things to accomplish
exist as a menu of choices, not a demand.

## World structure

- **Land = a key.** 14 lands total: 7 major keys + 7 minor keys, all on
  natural note names (C/D/E/F/G/A/B, each in both major and minor — not
  the relative-minor set, so no sharps/flats as a land's identity).
- **Village = one of that land's 7 diatonic chords** (I, ii, iii, IV, V,
  vi, vii° in a major land; i, ii°, III, iv, v, VI, VII in a minor one).
  `MusicTheory::romanNumeral()` already gives this labeling directly from
  a played chord relative to a key.
- **Mood/character falls out of harmonic function, not hand-authored
  lore**: tonic (I) = home/stable, dominant (V) = tense/driving, vii°
  (diminished) = unstable/dissonant, vi (relative minor) = melancholy,
  etc. A land can have a genuinely hard village or two without needing a
  villain to explain why — the harmony explains it.
- All 14 lands exist simultaneously in one open, explorable world. No
  land is locked behind another.

## Player

- Instrument: **a magic flute** — thematic/visual identity, not a
  literal note-input device. Casting a spell is a normal game action
  (select/trigger), not a rhythm-game performance check.
- **Theory drives the system, not literal input.** Real music-theory
  relationships shape spell naming, behavior, and balance — the player
  never has to actually play a correct chord in real time.
  - **Dominant** spells mechanically behave like tension-wanting-
    resolution (e.g. ramps in power, or rewards/demands a follow-up
    action) — without requiring the player to literally play a V chord.
  - **Diminished** spells are unstable BY DESIGN (powerful, but some
    randomness/uncertainty in outcome) — echoing a diminished chord's
    real ambiguity, not simulating it note-for-note.
  - **Consonance/dissonance maps to spell stability/feel** (using
    `consonanceScore()`'s existing values as a numeric input to
    behavior/VFX, not as something the player has to perform).
  - **Travel between lands follows the Circle of Fifths** — adjacent
    lands (a fifth apart) are the easy, natural routes; the tritone-
    opposite land is the hardest journey (literally "the devil's
    interval"). This is a world-layout rule, not a performance check
    either.

## Threats / survival

- **Real stakes — lethal, not a "nothing ever dies" game.** A wrong
  resolution can hurt you; a diminished spell can blow back on the
  caster; dissonant enemy attacks are real attacks.
- **Minions are ambient and everywhere** — the actual survival pressure,
  not a quest gate. They hate your flute specifically.
- **The "super bad" (final antagonist, no lore locked in yet — the
  recording-executive gag was dropped) sits in his own domain and does
  not come looking for you.** You have to choose to go there. His land
  is the harmonic far extreme (a Locrian-flavored wasteland was floated —
  built on a diminished tonic, no stable home chord at all) — open
  question below.
- **Open question, unresolved:** PvE-only danger, or does PvP exist too
  (can another player's flute actually end your run)? This changes how
  death needs to work (respawn cost vs. drop-your-progress vs. real
  permadeath) and should get pinned down before building the death/
  respawn system.

## Codebase context (as of this doc)

- `GroundPlane` today is a placeholder: one static flat quad, no height,
  no chunks, nothing procedural. SongScape's ground/rendering-management
  work starts from here, not from reviving the deleted `VoxelGrid`/
  `LivingGridRegion` (a different, CA-based mechanic that this replaces
  rather than restores).
- The uncommitted working-tree state (voxel → `BlockCharacter` FBX-avatar
  pivot) builds clean on both `Harmonia` and `HarmoniaServer` targets as
  of this session — verified before this design work started.
- `IRegion` (`Source/Client/World/Regions/IRegion.h`) is an existing
  abstraction with zero concrete implementations left after
  `LivingGridRegion` was deleted — worth checking whether Land/Village
  should be built as a new `IRegion`-shaped thing or as its own model;
  not yet decided.
- `MusicTheory.h`/`CircleOfFifths.h` already provide most of the raw
  primitives this needs: chord detection, roman-numeral analysis, scale
  tables (major/minor/modes), consonance scoring, circle-of-fifths
  position. This is additive work on an existing foundation, not a
  from-scratch system.

## Open questions (not yet answered)

1. PvP or PvE-only lethality?
2. What is the actual verb at a village — defeat (combat), solve
   (puzzle), something survival-shaped, or something else? (A literal
   music-performance/rhythm-game challenge is ruled out — see "Player"
   above: theory drives the system, not literal input.) Still
   undecided; blocks village-interaction code specifically (the Land/
   Village *data model* doesn't need this answered first).
3. Is there a name/identity for the "super bad" yet, now that the
   recording-executive framing is dropped?
4. Exact minor-key set: confirmed as the 7 natural letter names in minor
   mode (not relative minors of the 7 majors) — flagged during
   brainstorm, not yet explicitly re-confirmed by the user.
