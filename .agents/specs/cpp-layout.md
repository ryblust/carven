# cpp-layout.md

This guide supplements `cpp-format.md`. The format spec keeps the mechanical
rules; this layout/readability spec describes how to arrange C++ so the reader
can feel the logic move.

## Semantic Density
- Layout follows local semantic density, not one file-wide rhythm.
- Keep low-density code tight: simple forwarding, short predicates, map-like
  switches, obvious calls, and declaration plus immediate guard.
- Keep compact tables compact when the reader's job is lookup: token maps,
  small type maps, command maps, and obvious operator mappings.
- In symmetric lookup or dispatch code, one-line `case ...: return ...;` and
  one-line `if (...) return ...;` may be clearer than expanded blocks.
- Use blank lines in medium-density code to show phases: observe, validate,
  parse children, recover, construct.
- Give high-density code visual space when it changes reader mode: error
  recovery, synchronization, multi-stage grammar, or non-obvious control flow.
- Treat density mismatch as a layout smell. Code that is too loose feels
  hollow; code that is too tight hides meaning.

## Reading Rhythm
- Use blank lines as semantic paragraph boundaries, not decoration.
- Keep tightly coupled statements together.
- Keep one state transition tight when its steps are mechanical parts of the
  same action, such as consume prefix, scan characters, then return the token.
- Put a blank line between alternative branches when each branch handles a
  distinct spelling, mode, or result.
- Put a blank line after a branch setup when the following block switches from
  setup to repeated scanning, validation, recovery, or construction.
- Avoid more than one consecutive blank line.

## Control Flow
- Expand guards that carry semantic weight: error reporting, synchronization,
  cleanup, recovery, or an alternate result.
- Keep trivial forwarding and obvious predicates compact.
- Keep one-line guards when the branch is genuinely mechanical or part of a
  symmetric lookup/dispatch group.
- Expand state-machine transitions when the branch changes mode, consumes
  input, or updates error state.
- Expand resource and system-call failure paths even when the branch body is
  short.
- In loops, arrange code as inspect, decide, consume or transform, then update.
- Do not hide meaningful recovery in one-line blocks.

## Vertical Layout
- Use vertical layout for long conditions, grouped alternatives, and
  construction calls whose arguments represent distinct concepts.
- Align repeated alternatives only when the alignment forms a table the reader
  can scan.
- Prefer multi-line designated initializers for objects with named fields.
- Keep obvious calls compact.
