# Documentation Comments

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Source documentation attachment and documentation tooling input

## Summary

Carven has ordinary comments but no source form whose content is retained as
documentation for a module or declaration. This proposal will define the
source attachment contract and the compiler facts needed by future
documentation tools.

Source spelling (`OPEN-01`), attachment and retention (`OPEN-02`), markup
(`OPEN-03`), and the first output artifact (`OPEN-04`) remain open, in that order.

## Context

### Current behavior

The grammar defines `//` line comments as discarded lexical material. Block
comment spellings are opaque bytes only inside C++ source fragments. The lexer
does not distinguish documentation from ordinary comments, and later compiler
stages cannot associate comment content with a declaration. Carven also has no
documentation artifact or generator.

### Problem

Library authors need durable API explanations close to the declarations they
describe. Ordinary comments cannot support reliable association, validation,
or documentation generation because they disappear before parsing.

### Existing constraints

- Ordinary comments remain semantically inert.
- Documentation attachment is deterministic from source structure.
- A missing documentation tool does not change program validity or generated
  runtime behavior.
- The source model is independent of an output tool's conventions.
- Generated C++ is not automatically a stable public documentation interface.

## Goals and non-goals

### Goals

- Attach retained documentation text to a module or declaration.
- Preserve enough structure for future documentation tools.
- Diagnose ambiguous or invalid attachment locally.
- Give tools a stable Carven-level input without changing runtime behavior.

### Non-goals

- Selecting an HTML theme, hosting system, or documentation-site generator.
- Turning documentation markup into runtime reflection metadata.
- Requiring generated C++ comments to be the canonical documentation artifact.
- Exposing the compiler's private storage container as a public tooling API.

## Design

The following candidate illustrates module and declaration documentation.
It is not valid Carven documentation syntax today:

```carven
//! Describes the current module.

/// Adds two values.
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

Any selected design must preserve documentation as a distinct source event,
attach it according to syntax rather than name or type lookup, and reject
orphaned or ambiguous forms at the owning source range. The compiler must
retain the resulting content and attachment identity long enough for the
selected artifact consumer.

`OPEN-02` compares parser attachment with a source-indexed side table, including
behavior, diagnostics, and tooling consequences.

## Open decisions

**Next discussion:** `OPEN-01`

### OPEN-01 — Which source forms denote documentation?

- **Status:** Active
- **Question:** The spelling determines lexical compatibility,
  module-level documentation, and whether documentation is visually distinct
  from ordinary comments.
- **Constraints:** Recognition cannot require name or type lookup; module and
  declaration documentation both need unambiguous source forms.
- **Options:** A — Rust-style `//!` and `///` line forms; B — dedicated line
  forms plus documentation block comments; C — an attribute carrying string or
  structured content.
- **Closure condition:** Compare realistic module, declaration, field, enum-case,
  attribute, ordinary-comment, and multiline examples and select the smallest
  source set that represents them without ambiguity.

### OPEN-02 — Where is attachment resolved and stored?

- **Status:** Blocked
- **Depends on:** `OPEN-01`
- **Activation condition:** A source form has been selected.
- **Question:** The stage and representation determine how documentation
  survives parsing, where orphan/duplicate attachment is diagnosed, and which
  compiler facts future tools can consume.
- **Constraints:** Attachment is deterministic from source structure rather
  than later name/type lookup; diagnostics are local and stable; the selected
  representation handles module docs, declarations, fields, and enum cases.
- **Options:** A — retain distinct documentation tokens and attach them while
  constructing syntax nodes; B — retain source ranges in a side table and
  resolve attachment in a dedicated syntax pass.
- **Closure condition:** Prototype both choices against attributes, blank lines,
  consecutive documentation blocks, ordinary comments between forms,
  malformed declarations, and end of file; select the owner of attachment and
  the durable compiler fact.

### OPEN-03 — What markup contract does Carven expose?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`
- **Activation condition:** Retained text and attachment boundaries are known.
- **Question:** Markup controls portability between tools and determines
  whether the compiler must understand references inside documentation.
- **Constraints:** Documentation text cannot change program semantics; tools
  need one stable interpretation of links and code blocks.
- **Options:** A — preserve Markdown and leave rendering to tools; B — parse a
  fixed vocabulary of structured tags; C — preserve Markdown while extracting
  a small compiler-known set of references.
- **Closure condition:** Evaluate parameters, failure contracts,
  cross-references, and code snippets and identify which features require
  compiler knowledge.

### OPEN-04 — Which artifact first consumes documentation?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`, `OPEN-03`
- **Activation condition:** The retained content and markup contract are stable.
- **Question:** The first consumer determines the minimum durable compiler
  facts and the first end-to-end validation path.
- **Constraints:** Generated C++ is not automatically a stable public
  interface; documentation generation cannot affect ordinary execution.
- **Options:** A — documentation-oriented metadata consumed by a separate tool;
  B — compiler-integrated output; C — generated C++ comments with an explicitly
  weaker Carven-level contract.
- **Closure condition:** Define one end-to-end use case and compare artifact
  stability, cross-module lookup, and downstream-tool requirements.

## Implementation

Implementation is not actionable until `OPEN-01` and `OPEN-02` close. The
first vertical delivery should cover lexical retention, source attachment,
source ranges, the selected retained representation, compiler inspection, and
local diagnostics.

Artifact delivery follows the markup and output decisions in `OPEN-03` and
`OPEN-04`. The parser/inspection slice retains documentation without selecting
those contracts.

## Validation

Experiments used to close `OPEN-01` and `OPEN-02` must cover module and
declaration documentation, fields and enum cases, consecutive forms,
attributes, ordinary comments, blank lines, malformed or orphaned forms, and
end-of-file behavior.

The delivered feature must additionally prove compiler preservation and the
selected attachment representation. After `OPEN-03` and `OPEN-04`, artifact
validation must prove the selected output separately. Ordinary comments must
remain discarded and neither ordinary nor documentation comments may alter
generated program behavior.
