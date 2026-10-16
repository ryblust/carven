# Documentation

This document indexes Carven's references and defines their scope and maintenance
rules.

## Find a reference

| Reference | Scope |
| --- | --- |
| [Language](language/README.md) | Learning, syntax, program validity, and observable behavior |
| [Standard capabilities](language/standard.md) | Standard types, functions, module entries, and API references |
| [Toolchain](toolchain/README.md) | Compiler invocation, native requirements, generated artifacts, and build integration |
| [Compiler](compiler/README.md) | Semantic construction, analysis, execution, and C++ generation |
| [Development](development/README.md) | Coding conventions, validation, and design principles |

Runnable programs live in [examples](../examples/README.md); unfinished designs
live in [proposals](../proposals/README.md). Standard API references live in
`language/`; Xmake procedures are documented alongside their implementation.

The language tutorial introduces concepts in learning order. Grammar and topic
references define source rules.

## Maintenance

- State what to do and how, concisely and neutrally. Use imperative rules for
  development guidance and factual descriptions for language and implementation
  contracts. Omit design defenses, comparisons with removed behavior, and promises
  about future features or compatibility.
- Give each document a reader task. Keep rules in their owning reference and topic
  explanations self-contained. Put navigation in indexes; repeat only context needed
  to apply the local contract.
- Open with a brief statement of the document's scope. Put rules in their owning
  sections. Use tables for mappings, comparisons, and command reference; state
  prerequisites and execution order explicitly.
- Keep standard capability navigation in `language/standard.md`. Detailed contracts
  belong to the owning topic; module lookup and defaults belong to `language/modules.md`.
- Describe current behavior, requirements, and limitations in references. Check
  their accuracy against implementation and tests in the same checkout; maintain
  them with that code. Keep proposed changes in proposals.
- State supported forms, defaults, evaluation, lifetime, failure, and diagnostic
  boundaries explicitly.
- Preserve decision criteria, rule strength, preconditions, exceptions, and examples
  that distinguish choices. Move misplaced rules to their owning reference; remove
  duplicate explanations.
- Present teaching examples in dependency order, with commands and expected
  results where useful.
- Keep runnable examples and reference snippets aligned with current language
  and standard APIs. Use the simplest supported form that expresses their purpose.
