# Documentation

This directory contains Carven's tutorial, language and implementation references,
and development guidance for the current checkout.

## Domains

| Domain | Scope |
| --- | --- |
| [Language](language/README.md) | Learning, syntax, program validity, and observable behavior |
| [Toolchain](toolchain/README.md) | Compiler invocation, native requirements, generated artifacts, and build integration |
| [Compiler](compiler/README.md) | Semantic construction, analysis, execution, and C++ generation |
| [Development](development/README.md) | Coding conventions, validation, and design principles |

Runnable programs live in [examples](../examples/README.md); unfinished designs
live in [proposals](../proposals/README.md). Craft APIs and Xmake procedures are
documented alongside their sources.

The language tutorial introduces concepts in learning order. Grammar and the
topic references define source rules. Compiler documentation describes their
implementation; toolchain documentation defines how to invoke the compiler and
consume its output. Development documents guide changes to the project.
Each rule belongs to the reference that owns its scope.

Maintain the language and implementation references with the code they describe.
Their rules and mechanisms apply to the same checkout.

## Maintenance

- Write concise, factual, neutral explanations of what to do and how. Include
  mechanisms needed to understand a rule's behavior or use. Avoid defenses of
  design choices, comparisons with removed behavior, and promises about future
  features or compatibility.
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
