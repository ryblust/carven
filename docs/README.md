# Documentation

This directory contains Carven's tutorial, language and implementation references,
and development guidance for the current checkout.

## Documents

| Document | Scope |
| --- | --- |
| [Tutorial](tutorial.md) | Basic concepts and examples in learning order |
| [Grammar](grammar.md) | Encoding, tokens, syntax, precedence, and parsing |
| [Semantics](semantics.md) | Program validity and observable behavior |
| [CLI](cli.md) | Invocation, input paths, output writes, and process status |
| [Toolchain](toolchain.md) | Native requirements, artifact interfaces, compilation, linking, and build integration |
| [Compiler](compiler.md) | Orchestration, semantic analysis, ownership, dependencies, and publication |
| [Backend](backend.md) | Published semantics to C++ representation, target syntax, and emitted artifacts |
| [Conventions](conventions.md) | Handwritten C++ organization, naming, and coding rules |
| [Testing](testing.md) | Suite responsibilities, fixtures, assertions, and validation procedures |
| [Principles](principles.md) | Design principles and guidance for language and implementation decisions |

Runnable programs live in [examples](../examples/README.md); unfinished designs
live in [proposals](../proposals/README.md). Craft APIs and Xmake procedures are
documented alongside their sources.

Grammar and Semantics together define the language reference. Tutorial examples
introduce those rules in learning order. Compiler and Backend describe their
implementation; Principles states design criteria. Each rule belongs to the
reference that owns its scope.

## Maintenance

- Write concise, factual, neutral explanations of what to do and how. Include
  mechanisms and rationale where they explain a rule's behavior or use.
- Keep each rule in the document that owns its scope. Make each explanation
  locally understandable with the brief context it needs. Use cross-references
  only when essential detail cannot be stated concisely in place. Split documents
  by reader task.
- Describe current behavior, requirements, and limitations in references. Check
  their accuracy against implementation and tests; keep proposed changes in proposals.
- State supported forms, defaults, evaluation, lifetime, failure, and diagnostic
  boundaries explicitly.
- Preserve decision criteria, useful rationale, and practical guidance when
  condensing text. Combine repeated explanations.
- Present teaching examples in dependency order, with commands and expected
  results where useful.
