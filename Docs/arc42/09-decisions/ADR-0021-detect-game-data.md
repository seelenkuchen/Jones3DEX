# ADR-0021: Detect the installed game data at startup

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

ScummVM identifies game data before it starts an engine, and libultraship checks that its archive matches the
build ([research](../../Research/Engine-Patterns.md)). Jones3D doesn't check which data is installed. Wrong,
partial or modified data shows up later as load errors or crashes.

## Decision

- At startup the game checksums its GOB archives and compares them with a table of known releases, patch
  levels and languages.
- Known data: the version is logged and available to the game, for example to choose retail behavior that
  differs by version.
- Unknown or incomplete data: a clear warning naming the files, then the game continues. Loose-file overrides
  and mods stay possible.
- The table records provenance: which release each checksum comes from.

Implemented in M8 (2026-11-14): the game searches for the data at startup, asks for it with a folder
picker when it cannot find it, and checks it against the table. `--data`, `--hash-data` and
`--install-data` cover explicit folders, adding releases to the table, and installing from CDs.

## Consequences

- Needed for M8's game data installation; the table is built from real releases, never from guesses.
- Checksumming large GOBs costs startup time; results can be cached by file size and modification time.
- Bug reports and traces can name the exact data version.
