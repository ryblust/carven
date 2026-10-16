# Documentation Comments

- **Status:** Exploration
- **Scope:** Source documentation attachment and documentation tooling input

## Summary

Carven has ordinary comments but no source form whose content is retained as
documentation for a module or declaration. This proposal will define the
source attachment contract and the compiler facts needed by future
documentation tools.

The first consumer (`OPEN-04`) and source forms (`OPEN-01`) remain open. Their
requirements determine attachment and retention (`OPEN-02`) and markup
(`OPEN-03`).

## Context

### Current behavior

The lexer skips `//` line comments, including `///` and `//!`, without producing
documentation tokens. Immutable source owners and the formatter can preserve
the original comment bytes, but no compiler fact attaches them to a declaration.
Block-comment spellings are opaque bytes only inside C++ source fragments.
Carven has no documentation artifact or generator.

### Problem

Library authors need API explanations close to the declarations they describe.
The compiler currently provides neither an attachment rule nor a documentation
input for tools.

### Existing constraints

- Ordinary comments remain semantically inert.
- Documentation attachment is deterministic from source structure.
- Documentation tools consume established source facts; their presence does
  not change program validity or runtime behavior.
- The source model is independent of an output tool's conventions.
- Generated C++ is not automatically a stable public documentation interface.

## Design

The following candidate illustrates module and declaration documentation.
Today these lines are accepted as ordinary comments and carry no documentation
meaning:

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

The selected consumer determines the facts that need a durable interface. Themes,
hosting, runtime reflection, and private compiler containers are outside that
source contract.

## Open decisions

### OPEN-04 — Which artifact first consumes documentation?

- **Question:** The first consumer determines the minimum durable compiler
  facts and the end-to-end validation path.
- **Options:** A — documentation-oriented metadata consumed by a separate tool;
  B — compiler-integrated output; C — generated C++ comments with an explicitly
  weaker Carven-level contract.
- **Closure condition:** Define one end-to-end use case and compare artifact
  stability, cross-module lookup, and downstream-tool requirements.

### OPEN-01 — Which source forms denote documentation?

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

- **Depends on:** Source forms from `OPEN-01` and consumer requirements from
  `OPEN-04`.
- **Question:** The stage and representation determine how documentation
  survives parsing, where orphan/duplicate attachment is diagnosed, and which
  compiler facts future tools can consume.
- **Constraints:** The selected representation handles module documentation,
  declarations, fields, and enum cases.
- **Options:** A — retain distinct documentation tokens and attach them while
  constructing syntax nodes; B — retain source ranges in a side table and
  resolve attachment in a dedicated syntax pass.
- **Closure condition:** Prototype both choices against attributes, blank lines,
  consecutive documentation blocks, ordinary comments between forms,
  malformed declarations, and end of file; select the owner of attachment and
  the durable compiler fact.

### OPEN-03 — What markup contract does Carven expose?

- **Depends on:** Retained text and attachment boundaries from `OPEN-02`, and
  consumer requirements from `OPEN-04`.
- **Question:** Markup controls portability between tools and determines
  whether the compiler must understand references inside documentation.
- **Constraints:** Tools need one stable interpretation of links and code blocks.
- **Options:** A — preserve Markdown and leave rendering to tools; B — parse a
  fixed vocabulary of structured tags; C — preserve Markdown while extracting
  a small compiler-known set of references.
- **Closure condition:** Evaluate parameters, failure contracts,
  cross-references, and code snippets and identify which features require
  compiler knowledge.

## Delivery scope

Select one consumer and the source contract it needs before retaining new
compiler facts. The first delivery covers source recognition, attachment and
ranges, local diagnostics, and consumption of the retained documentation.
Choose the markup needed by that consumer as part of the same scope.

## Validation

Experiments used to close `OPEN-01` and `OPEN-02` must cover module and
declaration documentation, fields and enum cases, consecutive forms,
attributes, ordinary comments, blank lines, malformed or orphaned forms, and
end-of-file behavior.

Validate the selected consumer's output against source content and attachment,
including cross-module use. Ordinary comments have no documentation meaning by
default. Retaining either comment form must preserve generated program behavior.

## References

- [Grammar](../docs/language/grammar.md): ordinary comments and declarations.
- [Lexer](../src/frontend/lex/lex.cpp): current comment skipping.
- [Parsed documents](../src/frontend/document/document.cpp): retained source and syntax ownership.
