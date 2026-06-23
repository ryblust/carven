# Diagnostics

Status: implementation/tooling proposal; phase 1 implemented.

## Goal

Make diagnostics a first-class part of Carven without prematurely forcing an
intrusive compiler-wide diagnostic engine into the frontend and backend.

The first step is naming and ownership: `carven.driver.diagnostics` owns
user-facing diagnostic rendering for the CLI. Future steps can introduce a
shared diagnostic data model and, only when needed, a diagnostic engine for
policy-heavy reporting.

## Motivation

Carven currently has a simple error path:

- parser and semantic analysis return `ParseError` values
- driver commands render those errors with source locations and carets
- command parsing returns plain error messages in `std::expected`

That style is easy to reason about and works well with the project's
no-exception direction. The weakness is vocabulary: `ParseError` is too narrow
for semantic errors, warnings, notes, and fix-it hints, while `report` describes
an output action instead of the broader compiler concept.

`diagnostics` is the compiler-domain name that can grow from today's error
printing into a full diagnostic system.

## Current Status

The initial structural step is complete:

- `src/driver/diagnostics.cppm` exports `carven.driver.diagnostics`
- `report_errors(ParseError...)` remains the current adapter
- command parsing errors remain local to `carven.driver.command` as strings
- parser and sema still return diagnostic values instead of receiving an engine

## Proposed Direction

Keep the current design small:

- use `src/driver/diagnostics.cppm` and `carven.driver.diagnostics`
- keep `report_errors` as the current `ParseError` adapter
- keep parser and sema returning values instead of accepting a mutable engine
- keep terminal formatting in the driver layer
- keep command parsing errors in `carven.driver.command`

Then evolve the diagnostic model before introducing an engine:

```cpp
export enum class DiagnosticSeverity {
    Error,
    Warning,
    Note,
    Help,
};

export struct Diagnostic final {
    DiagnosticSeverity severity;
    std::string message;
    Span span;
};
```

Parser and semantic analysis can eventually return `std::vector<Diagnostic>`
or result types containing diagnostics. This keeps diagnostics explicit in the
data flow while allowing richer severity and message metadata.

## Engine Strategy

Do not introduce an intrusive `DiagnosticEngine` until the project needs policy
that simple returned values cannot express cleanly. Useful triggers include:

- warning groups and warning-as-error
- diagnostic codes such as `E0001`
- related notes and secondary spans
- fix-it hints
- error limits and fatal diagnostics
- color, no-color, JSON, quiet, or verbose output modes
- multi-file source management

Before that point, prefer a lightweight `DiagnosticBuffer` if collection becomes
awkward:

```cpp
export class DiagnosticBuffer final {
public:
    auto error(Span span, std::string message) noexcept -> void;
    auto warning(Span span, std::string message) noexcept -> void;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;
};
```

The buffer can remain local to parser, sema, or lowering code and return its
values at module boundaries. This avoids spreading mutable output state through
the compiler too early.

## Layering

Use two layers when the model grows:

- `carven.common.diagnostics`: pure diagnostic data types shared by frontend,
  backend, and driver
- `carven.driver.diagnostics`: CLI rendering, output mode policy, and adapters
  from older result types

Frontend and backend code should not depend on terminal formatting or process
output. Driver code may adapt frontend/backend diagnostics into user-facing
reports.

## Migration Plan

1. Keep the current `report_errors(ParseError...)` behavior unchanged.
2. Keep plain command parsing errors in `driver.command` unless they need
   severity, spans, or related notes.
3. Introduce `DiagnosticSeverity` and `Diagnostic` as value types once warnings,
   notes, or non-parser diagnostics need the same path.
4. Convert `ParseError` users gradually, preserving current returned-value data
   flow.
5. Add `DiagnosticBuffer` only if local collection starts duplicating helper code.
6. Add `DiagnosticEngine` only after policy requirements make it worthwhile.

## Non-Goals

- Do not introduce a global diagnostic engine as part of the current design.
- Do not make parser, sema, or backend depend on driver rendering.
- Do not change CLI output formatting during the structural refactor.
- Do not replace `ParseError` until there is a concrete feature that benefits
  from a richer diagnostic model.

## Open Questions

- Should diagnostic codes be stable user-facing identifiers or internal test
  aids first?
- Should JSON diagnostics be designed before or after multi-file source
  management?
- Should source rendering eventually support secondary spans and notes in one
  report, or should notes be rendered as separate diagnostics?
